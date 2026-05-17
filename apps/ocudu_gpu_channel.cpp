#include "ocudu_gpu_channel/backend.h"
#include "ocudu_gpu_channel/broker.h"
#include "ocudu_gpu_channel/config.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage()
{
  std::cout << "usage: ocudu-gpu-channel --config topology.yaml [--duration 60s] [--strict-realtime]\n";
}

std::chrono::milliseconds parse_duration(const std::string& value)
{
  if (value.ends_with("ms")) {
    return std::chrono::milliseconds(std::stoll(value.substr(0, value.size() - 2)));
  }
  if (value.ends_with('s')) {
    return std::chrono::seconds(std::stoll(value.substr(0, value.size() - 1)));
  }
  return std::chrono::milliseconds(std::stoll(value));
}

} // namespace

int main(int argc, char** argv)
{
  std::string config_path;
  std::chrono::milliseconds duration{0};
  bool strict_realtime = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    }
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--duration" && i + 1 < argc) {
      duration = parse_duration(argv[++i]);
    } else if (arg == "--strict-realtime") {
      strict_realtime = true;
    } else {
      std::cerr << "unknown or incomplete argument: " << arg << "\n";
      usage();
      return 2;
    }
  }

  if (config_path.empty()) {
    usage();
    return 2;
  }

  try {
    auto config = ocg::load_config_file(config_path);
    std::cout << "event=start backend=" << ocg::to_string(config.runtime.backend)
              << " cuda_status=" << ocg::backend_status() << "\n";
    ocg::Broker broker(std::move(config));
    auto stats = broker.run(duration);
    std::cout << "event=stop tx_pulls=" << stats.tx_pulls << " rx_requests=" << stats.rx_requests
              << " rx_starvations=" << stats.rx_starvations << " tx_queue_overflows=" << stats.tx_queue_overflows
              << " tx_sequence_gaps=" << stats.tx_sequence_gaps << " zmq_errors=" << stats.zmq_errors << "\n";
    if (strict_realtime &&
        (stats.rx_starvations != 0 || stats.tx_queue_overflows != 0 || stats.tx_sequence_gaps != 0 || stats.zmq_errors != 0)) {
      std::cerr << "event=unstable reason=\"strict realtime counters were nonzero\"\n";
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "event=fatal error=\"" << e.what() << "\"\n";
    return 1;
  }

  return 0;
}
