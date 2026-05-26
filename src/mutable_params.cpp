#include "ocudu_gpu_channel/mutable_params.h"

#include "ocudu_gpu_channel/config.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace ocg {

namespace {

double param_or(const ModelStep& step, const std::string& name, double fallback)
{
  const auto it = step.params.find(name);
  return it == step.params.end() ? fallback : it->second;
}

}  // namespace

MutableParams populate_mutable_params_from_yaml(
    const ModelConfig& model,
    double reference_power,
    std::uint64_t sample_rate_hz)
{
  (void)sample_rate_hz;   // not used in v1; kept for signature symmetry

  MutableParams out;

  // Scan chain for the first PathLoss / Cfo / Awgn step.
  for (const auto& step : model.chain) {
    switch (step.type) {
      case ModelStepType::PathLoss:
        out.path_loss_db = static_cast<float>(param_or(step, "path_loss_db", 0.0));
        break;
      case ModelStepType::Cfo:
      case ModelStepType::Phase:
        out.cfo_hz = static_cast<float>(param_or(step, "cfo_hz", 0.0));
        break;
      case ModelStepType::Awgn: {
        double noise_power = param_or(step, "noise_power", -1.0);
        if (noise_power < 0.0) {
          const double snr_db = param_or(step, "snr_db", 60.0);
          noise_power = reference_power / std::pow(10.0, snr_db / 10.0);
        }
        noise_power = std::max(0.0, noise_power);
        out.awgn_sigma = static_cast<float>(std::sqrt(noise_power / 2.0));
        break;
      }
      case ModelStepType::Tdl:
        // Tap params handled below from chain[0].taps[0].
        break;
    }
  }

  // Leading tap of the leading tdl step (if present).
  if (!model.chain.empty() && model.chain.front().type == ModelStepType::Tdl) {
    const auto& leading = model.chain.front();
    if (!leading.taps.empty()) {
      const auto& tap0 = leading.taps.front();
      out.tap0_delay_samples = static_cast<int32_t>(std::lround(tap0.delay_samples));
      out.tap0_gain_db       = static_cast<float>(tap0.gain_db);
      out.tap0_phase_rad     = static_cast<float>(tap0.phase_rad);
      out.los_k_db           = static_cast<float>(tap0.los_k_db);
    }
  }

  return out;
}

}  // namespace ocg
