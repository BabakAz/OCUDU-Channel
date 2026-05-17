#pragma once

#include "ocudu_gpu_channel/config.h"
#include "ocudu_gpu_channel/iq.h"
#include <memory>
#include <random>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ocg {

struct ProcessorTimings {
  double h2d_us = 0.0;
  double kernel_us = 0.0;
  double d2h_us = 0.0;
  double gpu_process_us = 0.0;
};

class ChannelProcessor {
public:
  virtual ~ChannelProcessor() = default;

  virtual void prepare(const TopologyConfig& config) = 0;

  virtual void process_into(const std::string& link_key,
                            const ModelConfig& model,
                            std::span<const IqSample> input,
                            std::span<IqSample> output,
                            std::uint64_t sample_rate_hz) = 0;

  virtual ProcessorTimings last_timings() const = 0;
  virtual const char* backend_name() const = 0;
};

class CpuChannelProcessor final : public ChannelProcessor {
public:
  void prepare(const TopologyConfig& config) override;

  void process_into(const std::string& link_key,
                    const ModelConfig& model,
                    std::span<const IqSample> input,
                    std::span<IqSample> output,
                    std::uint64_t sample_rate_hz) override;

  ProcessorTimings last_timings() const override { return {}; }
  const char* backend_name() const override { return "cpu"; }

  IqBuffer process(const std::string& link_key,
                   const ModelConfig& model,
                   const IqBuffer& input,
                   std::uint64_t sample_rate_hz);

  IqBuffer mix_for_destination(const TopologyConfig& config,
                               const std::string& destination_id,
                               const std::unordered_map<std::string, IqBuffer>& latest_tx);

private:
  struct ModelState {
    bool seeded = false;
    std::vector<IqSample> delay_line;
    IqBuffer scratch_a;
    IqBuffer scratch_b;
    double phase_rad = 0.0;
    std::mt19937 rng;
  };

  std::unordered_map<std::string, ModelState> states_;
};

std::vector<std::string> validate_cuda_support(const TopologyConfig& config);
std::unique_ptr<ChannelProcessor> create_channel_processor(const TopologyConfig& config);

} // namespace ocg
