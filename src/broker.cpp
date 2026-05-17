#include "ocudu_gpu_channel/broker.h"
#include "ocudu_gpu_channel/ring.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zmq.h>

namespace ocg {
namespace {

std::atomic_bool stop_requested = false;

void signal_handler(int)
{
  stop_requested.store(true);
}

struct SocketDeleter {
  void operator()(void* socket) const
  {
    if (socket != nullptr) {
      zmq_close(socket);
    }
  }
};

using SocketPtr = std::unique_ptr<void, SocketDeleter>;

struct ContextDeleter {
  void operator()(void* context) const
  {
    if (context != nullptr) {
      zmq_ctx_shutdown(context);
      zmq_ctx_destroy(context);
    }
  }
};

using ContextPtr = std::unique_ptr<void, ContextDeleter>;

void set_int_option(void* socket, int option, int value)
{
  if (zmq_setsockopt(socket, option, &value, sizeof(value)) != 0) {
    throw std::runtime_error(std::string("zmq_setsockopt failed: ") + zmq_strerror(zmq_errno()));
  }
}

SocketPtr make_socket(void* context, int type)
{
  void* socket = zmq_socket(context, type);
  if (socket == nullptr) {
    throw std::runtime_error(std::string("zmq_socket failed: ") + zmq_strerror(zmq_errno()));
  }
  set_int_option(socket, ZMQ_RCVTIMEO, 0);
  set_int_option(socket, ZMQ_SNDTIMEO, 0);
  set_int_option(socket, ZMQ_LINGER, 0);
  set_int_option(socket, ZMQ_RCVHWM, 4);
  set_int_option(socket, ZMQ_SNDHWM, 4);
  return SocketPtr(socket);
}

bool recv_samples_into(void* socket, std::span<IqSample> out, std::size_t& sample_count, int flags)
{
  zmq_msg_t msg;
  zmq_msg_init(&msg);
  const int nbytes = zmq_msg_recv(&msg, socket, flags);
  if (nbytes < 0) {
    const int err = zmq_errno();
    zmq_msg_close(&msg);
    if (err == EAGAIN) {
      return false;
    }
    if (err == EFSM) {
      throw std::runtime_error("zmq_msg_recv reached invalid REQ/REP state");
    }
    throw std::runtime_error(std::string("zmq_msg_recv failed: ") + zmq_strerror(err));
  }

  if (static_cast<std::size_t>(nbytes) % sizeof(IqSample) != 0) {
    zmq_msg_close(&msg);
    throw std::runtime_error("received ZMQ payload is not aligned to cf32 IQ samples");
  }

  sample_count = static_cast<std::size_t>(nbytes) / sizeof(IqSample);
  if (sample_count > out.size()) {
    zmq_msg_close(&msg);
    throw std::runtime_error("received ZMQ payload exceeds preallocated TX batch buffer");
  }

  std::memcpy(out.data(), zmq_msg_data(&msg), static_cast<std::size_t>(nbytes));
  zmq_msg_close(&msg);
  return true;
}

bool send_samples(void* socket, const IqBuffer& samples, int flags)
{
  const auto nbytes = samples.size() * sizeof(IqSample);
  const int sent = zmq_send(socket, samples.data(), nbytes, flags);
  if (sent < 0) {
    const int err = zmq_errno();
    if (err == EAGAIN) {
      return false;
    }
    if (err == EFSM) {
      throw std::runtime_error("zmq_send samples reached invalid REQ/REP state");
    }
    throw std::runtime_error(std::string("zmq_send samples failed: ") + zmq_strerror(err));
  }
  return static_cast<std::size_t>(sent) == nbytes;
}

bool send_request(void* socket, int flags)
{
  const std::uint8_t dummy = 0;
  const int sent = zmq_send(socket, &dummy, sizeof(dummy), flags);
  if (sent < 0) {
    const int err = zmq_errno();
    if (err == EAGAIN) {
      return false;
    }
    if (err == EFSM) {
      throw std::runtime_error("zmq_send request reached invalid REQ/REP state");
    }
    throw std::runtime_error(std::string("zmq_send request failed: ") + zmq_strerror(err));
  }
  return sent == static_cast<int>(sizeof(dummy));
}

struct DeviceSockets {
  const DeviceConfig* device = nullptr;
  SocketPtr tx_req;
  SocketPtr rx_rep;
  IqRing tx_ring;
  bool tx_request_pending = false;
  bool rx_reply_pending = false;
  IqBuffer tx_recv;
  IqBuffer rx_reply;
};

enum class PollKind {
  TxSend,
  TxRecv,
  RxRecv,
  RxSend
};

struct PollRef {
  std::size_t device_index = 0;
  PollKind kind = PollKind::TxSend;
};

struct LinkCursor {
  bool initialized = false;
  std::uint64_t next_sequence = 0;
};

std::string link_key(const LinkConfig& link)
{
  return link.from + ">" + link.to + ":" + link.model;
}

std::size_t device_index_by_id(const std::vector<DeviceSockets>& devices, const std::string& id)
{
  for (std::size_t i = 0; i != devices.size(); ++i) {
    if (devices[i].device->id == id) {
      return i;
    }
  }
  throw std::runtime_error("unknown device id: " + id);
}

void release_consumed_samples(std::vector<DeviceSockets>& devices,
                              const TopologyConfig& config,
                              const std::unordered_map<std::string, LinkCursor>& link_cursors)
{
  for (auto& device : devices) {
    bool has_outgoing_link = false;
    bool has_initialized_consumer = false;
    std::uint64_t min_cursor = device.tx_ring.next_sequence();

    for (const auto& link : config.links) {
      if (link.from != device.device->id) {
        continue;
      }
      has_outgoing_link = true;
      auto cursor_it = link_cursors.find(link_key(link));
      if (cursor_it == link_cursors.end() || !cursor_it->second.initialized) {
        min_cursor = device.tx_ring.earliest_sequence();
        continue;
      }
      has_initialized_consumer = true;
      min_cursor = std::min(min_cursor, cursor_it->second.next_sequence);
    }

    if (!has_outgoing_link) {
      device.tx_ring.discard_before(device.tx_ring.next_sequence());
    } else if (has_initialized_consumer) {
      device.tx_ring.discard_before(min_cursor);
    }
  }
}

} // namespace

Broker::Broker(TopologyConfig config) : config_(std::move(config))
{
  auto errors = validate_config(config_);
  if (!errors.empty()) {
    throw std::runtime_error("invalid topology: " + errors.front());
  }
  processor_ = create_channel_processor(config_);
}

BrokerStats Broker::run(std::chrono::milliseconds duration)
{
  stop_requested.store(false);
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  ContextPtr context(zmq_ctx_new());
  if (context == nullptr) {
    throw std::runtime_error(std::string("zmq_ctx_new failed: ") + zmq_strerror(zmq_errno()));
  }

  BrokerStats stats;
  std::vector<DeviceSockets> sockets;
  sockets.reserve(config_.devices.size());
  std::unordered_map<std::string, LinkCursor> link_cursors;
  std::unordered_map<std::string, IqBuffer> link_inputs;
  std::unordered_map<std::string, IqBuffer> link_outputs;

  for (const auto& device : config_.devices) {
    DeviceSockets entry;
    entry.device = &device;
    entry.tx_req = make_socket(context.get(), ZMQ_REQ);
    entry.rx_rep = make_socket(context.get(), ZMQ_REP);
    entry.tx_ring.reset(config_.runtime.queue_samples);
    const auto batch_samples = resolve_batch_samples(config_.runtime, device.sample_rate_hz);
    entry.tx_recv.resize(batch_samples);
    entry.rx_reply.resize(batch_samples);

    if (zmq_connect(entry.tx_req.get(), device.tx_endpoint.c_str()) != 0) {
      throw std::runtime_error("failed to connect TX REQ for " + device.id + ": " + zmq_strerror(zmq_errno()));
    }
    if (zmq_bind(entry.rx_rep.get(), device.rx_endpoint.c_str()) != 0) {
      throw std::runtime_error("failed to bind RX REP for " + device.id + ": " + zmq_strerror(zmq_errno()));
    }
    std::cout << "event=socket_ready device=" << device.id << " tx_connect=" << device.tx_endpoint
              << " rx_bind=" << device.rx_endpoint << "\n";
    sockets.push_back(std::move(entry));
  }

  for (const auto& link : config_.links) {
    const auto* destination = find_device(config_, link.to);
    if (destination == nullptr) {
      continue;
    }
    const std::size_t count = resolve_batch_samples(config_.runtime, destination->sample_rate_hz);
    const auto key = link_key(link);
    link_inputs[key].resize(count);
    link_outputs[key].resize(count);
    link_cursors.emplace(key, LinkCursor{});
  }

  std::vector<zmq_pollitem_t> poll_items;
  std::vector<PollRef> poll_refs;
  poll_items.reserve(sockets.size() * 2);
  poll_refs.reserve(sockets.size() * 2);

  const auto start = std::chrono::steady_clock::now();

  while (!stop_requested.load()) {
    if (duration.count() > 0 && std::chrono::steady_clock::now() - start >= duration) {
      break;
    }

    poll_items.clear();
    poll_refs.clear();
    for (std::size_t i = 0; i != sockets.size(); ++i) {
      auto& entry = sockets[i];
      if (entry.tx_request_pending) {
        poll_items.push_back({entry.tx_req.get(), 0, ZMQ_POLLIN, 0});
        poll_refs.push_back({i, PollKind::TxRecv});
      } else if (entry.tx_ring.free_capacity() >= resolve_batch_samples(config_.runtime, entry.device->sample_rate_hz)) {
        poll_items.push_back({entry.tx_req.get(), 0, ZMQ_POLLOUT, 0});
        poll_refs.push_back({i, PollKind::TxSend});
      }

      if (entry.rx_reply_pending) {
        poll_items.push_back({entry.rx_rep.get(), 0, ZMQ_POLLOUT, 0});
        poll_refs.push_back({i, PollKind::RxSend});
      } else {
        poll_items.push_back({entry.rx_rep.get(), 0, ZMQ_POLLIN, 0});
        poll_refs.push_back({i, PollKind::RxRecv});
      }
    }

    const int ready = zmq_poll(poll_items.data(), static_cast<int>(poll_items.size()), 1);
    if (ready < 0) {
      const int err = zmq_errno();
      if (err == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("zmq_poll failed: ") + zmq_strerror(err));
    }

    bool did_work = ready > 0;
    for (std::size_t poll_index = 0; poll_index != poll_items.size(); ++poll_index) {
      if (poll_items[poll_index].revents == 0) {
        continue;
      }

      auto& entry = sockets[poll_refs[poll_index].device_index];
      switch (poll_refs[poll_index].kind) {
        case PollKind::TxSend:
          if ((poll_items[poll_index].revents & ZMQ_POLLOUT) != 0 && send_request(entry.tx_req.get(), ZMQ_DONTWAIT)) {
            entry.tx_request_pending = true;
          }
          break;
        case PollKind::TxRecv:
          if ((poll_items[poll_index].revents & ZMQ_POLLIN) != 0) {
            std::size_t sample_count = 0;
            if (recv_samples_into(entry.tx_req.get(), entry.tx_recv, sample_count, ZMQ_DONTWAIT)) {
              if (!entry.tx_ring.push(std::span<const IqSample>(entry.tx_recv.data(), sample_count))) {
                ++stats.tx_queue_overflows;
              }
              entry.tx_request_pending = false;
              ++stats.tx_pulls;
            }
          }
          break;
        case PollKind::RxRecv: {
          if ((poll_items[poll_index].revents & ZMQ_POLLIN) == 0) {
            break;
          }
          std::uint8_t dummy = 0;
          const int received = zmq_recv(entry.rx_rep.get(), &dummy, sizeof(dummy), ZMQ_DONTWAIT);
          if (received < 0) {
            const int err = zmq_errno();
            if (err == EAGAIN) {
              break;
            }
            if (err == EFSM) {
              throw std::runtime_error("rx request reached invalid REQ/REP state");
            }
            throw std::runtime_error(std::string("rx request failed: ") + zmq_strerror(err));
          }

          std::fill(entry.rx_reply.begin(), entry.rx_reply.end(), IqSample{});
          bool has_source = false;
          for (const auto& link : config_.links) {
            if (link.to != entry.device->id) {
              continue;
            }
            const auto source_index = device_index_by_id(sockets, link.from);
            auto& source_ring = sockets[source_index].tx_ring;
            const auto key = link_key(link);
            auto& cursor = link_cursors[key];
            auto& input = link_inputs[key];
            auto& output = link_outputs[key];

            if (!cursor.initialized) {
              cursor.next_sequence = source_ring.earliest_sequence();
              cursor.initialized = true;
            }
            if (cursor.next_sequence < source_ring.earliest_sequence()) {
              cursor.next_sequence = source_ring.earliest_sequence();
              ++stats.tx_sequence_gaps;
            }
            if (!source_ring.read(cursor.next_sequence, input)) {
              continue;
            }

            const auto* source = find_device(config_, link.from);
            const auto* model = find_model(config_, link.model);
            if (source == nullptr || model == nullptr) {
              continue;
            }
            processor_->process_into(key, *model, input, output, source->sample_rate_hz);
            for (std::size_t sample_idx = 0; sample_idx != entry.rx_reply.size(); ++sample_idx) {
              entry.rx_reply[sample_idx] += output[sample_idx];
            }
            cursor.next_sequence += input.size();
            has_source = true;
          }
          if (!has_source) {
            ++stats.rx_starvations;
          }
          entry.rx_reply_pending = true;
          break;
        }
        case PollKind::RxSend:
          if ((poll_items[poll_index].revents & ZMQ_POLLOUT) != 0 &&
              send_samples(entry.rx_rep.get(), entry.rx_reply, ZMQ_DONTWAIT)) {
            entry.rx_reply_pending = false;
            ++stats.rx_requests;
          }
          break;
      }
    }

    if (!did_work) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    release_consumed_samples(sockets, config_, link_cursors);
  }

  sockets.clear();
  return stats;
}

} // namespace ocg
