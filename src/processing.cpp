#include "ocudu_gpu_channel/processing.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace ocg {
namespace {

double param_or(const ModelStep& step, const std::string& name, double fallback)
{
  auto it = step.params.find(name);
  return it == step.params.end() ? fallback : it->second;
}

double estimate_average_power(std::span<const IqSample> input)
{
  if (input.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (const auto& sample : input) {
    sum += power(sample);
  }
  return sum / static_cast<double>(input.size());
}

IqBuffer resize_or_pad(const IqBuffer& input, std::size_t size)
{
  IqBuffer out(size);
  const std::size_t copy_count = std::min(size, input.size());
  std::copy_n(input.begin(), copy_count, out.begin());
  return out;
}

bool cuda_step_supported(ModelStepType type)
{
  return type == ModelStepType::Gain || type == ModelStepType::PathLoss || type == ModelStepType::Phase ||
         type == ModelStepType::Cfo;
}

} // namespace

#if OCUDU_GPU_CHANNEL_HAS_CUDA
std::unique_ptr<ChannelProcessor> make_cuda_processor(const TopologyConfig& config);
#endif

void CpuChannelProcessor::prepare(const TopologyConfig& config)
{
  for (const auto& link : config.links) {
    const auto* destination = find_device(config, link.to);
    if (destination == nullptr) {
      continue;
    }
    const std::size_t count = resolve_batch_samples(config.runtime, destination->sample_rate_hz);
    auto& state = states_[link.from + ">" + link.to + ":" + link.model + ":0"];
    state.scratch_a.resize(count);
    state.scratch_b.resize(count);
  }
}

void CpuChannelProcessor::process_into(const std::string& link_key,
                                       const ModelConfig& model,
                                       std::span<const IqSample> input,
                                       std::span<IqSample> output,
                                       std::uint64_t sample_rate_hz)
{
  if (output.size() != input.size()) {
    throw std::runtime_error("process_into input and output sizes must match");
  }
  if (input.empty()) {
    return;
  }

  auto& base_state = states_[link_key + ":0"];
  if (base_state.scratch_a.size() < input.size()) {
    base_state.scratch_a.resize(input.size());
  }
  if (base_state.scratch_b.size() < input.size()) {
    base_state.scratch_b.resize(input.size());
  }

  std::copy(input.begin(), input.end(), base_state.scratch_a.begin());
  std::span<IqSample> current(base_state.scratch_a.data(), input.size());
  std::span<IqSample> next(base_state.scratch_b.data(), input.size());

  for (std::size_t step_index = 0; step_index != model.chain.size(); ++step_index) {
    const auto& step = model.chain[step_index];
    const std::string state_key = link_key + ":" + std::to_string(step_index);
    auto& state = states_[state_key];
    if (!state.seeded) {
      state.rng.seed(static_cast<unsigned>(std::hash<std::string>{}(state_key)));
      state.seeded = true;
    }

    switch (step.type) {
      case ModelStepType::Gain: {
        const float factor = static_cast<float>(std::pow(10.0, param_or(step, "gain_db", 0.0) / 20.0));
        for (std::size_t i = 0; i != current.size(); ++i) {
          next[i] = scale(current[i], factor);
        }
        break;
      }
      case ModelStepType::PathLoss: {
        const float factor = static_cast<float>(std::pow(10.0, -param_or(step, "path_loss_db", 0.0) / 20.0));
        for (std::size_t i = 0; i != current.size(); ++i) {
          next[i] = scale(current[i], factor);
        }
        break;
      }
      case ModelStepType::Awgn: {
        double noise_power = param_or(step, "noise_power", -1.0);
        if (noise_power < 0.0) {
          const double snr_db = param_or(step, "snr_db", 60.0);
          noise_power = estimate_average_power(current) / std::pow(10.0, snr_db / 10.0);
        }
        const double sigma = std::sqrt(std::max(0.0, noise_power) / 2.0);
        std::normal_distribution<float> dist(0.0F, static_cast<float>(sigma));
        for (std::size_t i = 0; i != current.size(); ++i) {
          next[i] = {current[i].i + dist(state.rng), current[i].q + dist(state.rng)};
        }
        break;
      }
      case ModelStepType::IntegerDelay:
      case ModelStepType::FractionalDelay: {
        const double requested_delay = param_or(step, "delay_samples", 0.0);
        const auto integer_delay = static_cast<std::size_t>(std::max(0.0, std::floor(requested_delay)));
        const double fraction = step.type == ModelStepType::FractionalDelay ? requested_delay - std::floor(requested_delay)
                                                                            : 0.0;
        const std::size_t history_size = integer_delay + 2;
        if (state.delay_line.size() < history_size) {
          state.delay_line.insert(state.delay_line.begin(), history_size - state.delay_line.size(), {});
        }

        auto sample_at = [&](std::ptrdiff_t index) {
          if (index >= 0) {
            return current[static_cast<std::size_t>(index)];
          }
          return state.delay_line[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(state.delay_line.size()) + index)];
        };

        for (std::size_t n = 0; n != current.size(); ++n) {
          const auto delayed_index = static_cast<std::ptrdiff_t>(n) - static_cast<std::ptrdiff_t>(integer_delay);
          const IqSample sample0 = sample_at(delayed_index);
          const IqSample sample1 = sample_at(delayed_index - 1);
          next[n].i = static_cast<float>((1.0 - fraction) * sample0.i + fraction * sample1.i);
          next[n].q = static_cast<float>((1.0 - fraction) * sample0.q + fraction * sample1.q);
        }

        if (current.size() >= history_size) {
          state.delay_line.assign(current.end() - static_cast<std::ptrdiff_t>(history_size), current.end());
        } else {
          const std::size_t keep_old = history_size - current.size();
          std::move(state.delay_line.end() - static_cast<std::ptrdiff_t>(keep_old), state.delay_line.end(), state.delay_line.begin());
          std::copy(current.begin(), current.end(), state.delay_line.begin() + static_cast<std::ptrdiff_t>(keep_old));
        }
        break;
      }
      case ModelStepType::Phase:
      case ModelStepType::Cfo: {
        const double fixed_phase = param_or(step, "phase_rad", 0.0);
        const double cfo_hz = param_or(step, "cfo_hz", 0.0);
        const double phase_increment =
            sample_rate_hz == 0 ? 0.0 : 2.0 * std::numbers::pi * cfo_hz / static_cast<double>(sample_rate_hz);
        for (std::size_t i = 0; i != current.size(); ++i) {
          next[i] = rotate(current[i], fixed_phase + state.phase_rad);
          state.phase_rad += phase_increment;
          if (state.phase_rad > 2.0 * std::numbers::pi) {
            state.phase_rad = std::fmod(state.phase_rad, 2.0 * std::numbers::pi);
          }
        }
        break;
      }
    }
    std::swap(current, next);
  }

  std::copy(current.begin(), current.end(), output.begin());
}

IqBuffer CpuChannelProcessor::process(const std::string& link_key,
                                      const ModelConfig& model,
                                      const IqBuffer& input,
                                      std::uint64_t sample_rate_hz)
{
  IqBuffer output(input.size());
  process_into(link_key, model, input, output, sample_rate_hz);
  return output;
}

IqBuffer CpuChannelProcessor::mix_for_destination(const TopologyConfig& config,
                                                  const std::string& destination_id,
                                                  const std::unordered_map<std::string, IqBuffer>& latest_tx)
{
  const auto* destination = find_device(config, destination_id);
  if (destination == nullptr) {
    throw std::runtime_error("unknown destination device: " + destination_id);
  }

  IqBuffer mixed(resolve_batch_samples(config.runtime, destination->sample_rate_hz));
  bool has_source = false;

  for (const auto& link : config.links) {
    if (link.to != destination_id) {
      continue;
    }

    auto tx_it = latest_tx.find(link.from);
    if (tx_it == latest_tx.end()) {
      continue;
    }

    const auto* source = find_device(config, link.from);
    const auto* model = find_model(config, link.model);
    if (source == nullptr || model == nullptr) {
      continue;
    }

    const std::size_t target_size = std::max(mixed.size(), tx_it->second.size());
    if (mixed.size() < target_size) {
      mixed.resize(target_size);
    }

    IqBuffer input = resize_or_pad(tx_it->second, target_size);
    IqBuffer processed =
        process(link.from + ">" + link.to + ":" + link.model, *model, input, source->sample_rate_hz);
    for (std::size_t i = 0; i != target_size; ++i) {
      mixed[i] += processed[i];
    }
    has_source = true;
  }

  if (!has_source) {
    return {};
  }
  return mixed;
}

std::vector<std::string> validate_cuda_support(const TopologyConfig& config)
{
  std::vector<std::string> errors;
  for (const auto& link : config.links) {
    const auto* model = find_model(config, link.model);
    if (model == nullptr) {
      continue;
    }
    for (const auto& step : model->chain) {
      if (!cuda_step_supported(step.type)) {
        errors.emplace_back("CUDA backend does not support model " + model->id + " step " + to_string(step.type));
      }
    }
  }
  return errors;
}

std::unique_ptr<ChannelProcessor> create_channel_processor(const TopologyConfig& config)
{
  if (config.runtime.backend == Backend::Cpu) {
    auto processor = std::make_unique<CpuChannelProcessor>();
    processor->prepare(config);
    return processor;
  }

  auto cuda_errors = validate_cuda_support(config);
  if (!cuda_errors.empty()) {
    std::ostringstream oss;
    oss << "invalid CUDA topology:";
    for (const auto& error : cuda_errors) {
      oss << "\n- " << error;
    }
    throw std::runtime_error(oss.str());
  }

#if OCUDU_GPU_CHANNEL_HAS_CUDA
  auto processor = make_cuda_processor(config);
  processor->prepare(config);
  return processor;
#else
  throw std::runtime_error("topology requested CUDA backend, but this build was not compiled with CUDA");
#endif
}

} // namespace ocg
