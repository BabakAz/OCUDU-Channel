#include "ocudu_gpu_channel/config.h"
#include "ocudu_gpu_channel/mutable_params.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

bool nearly(float a, float b, float tol = 1e-6F)
{
  return std::fabs(a - b) <= tol;
}

ocg::ModelStep make_path_loss_step(double db)
{
  ocg::ModelStep s;
  s.type = ocg::ModelStepType::PathLoss;
  s.params["path_loss_db"] = db;
  return s;
}

ocg::ModelStep make_cfo_step(double hz)
{
  ocg::ModelStep s;
  s.type = ocg::ModelStepType::Cfo;
  s.params["cfo_hz"] = hz;
  return s;
}

ocg::ModelStep make_awgn_step(double snr_db)
{
  ocg::ModelStep s;
  s.type = ocg::ModelStepType::Awgn;
  s.params["snr_db"] = snr_db;
  return s;
}

ocg::ModelStep make_tdl_step(double delay_samples, double gain_db, double phase_rad, double los_k_db)
{
  ocg::ModelStep s;
  s.type = ocg::ModelStepType::Tdl;
  ocg::TapSpec tap;
  tap.delay_samples = delay_samples;
  tap.gain_db = gain_db;
  tap.phase_rad = phase_rad;
  tap.los_k_db = los_k_db;
  s.taps.push_back(tap);
  return s;
}

}  // namespace

int main()
{
  // ── Case 1: full chain — every v1 param present ──────────────────────────
  {
    ocg::ModelConfig m;
    m.id = "full";
    m.chain.push_back(make_tdl_step(/*delay=*/5.5, /*gain_db=*/-3.0,
                                    /*phase_rad=*/0.25, /*los_k_db=*/9.0));
    m.chain.push_back(make_path_loss_step(/*db=*/12.0));
    m.chain.push_back(make_cfo_step(/*hz=*/250.0));
    m.chain.push_back(make_awgn_step(/*snr_db=*/20.0));

    const ocg::MutableParams live =
        ocg::populate_mutable_params_from_yaml(m, /*reference_power=*/1.0,
                                               /*sample_rate_hz=*/23040000);

    require(nearly(live.path_loss_db, 12.0F), "path_loss_db should be 12");
    require(nearly(live.cfo_hz, 250.0F), "cfo_hz should be 250");
    require(live.tap0_delay_samples == 6, "tap0_delay_samples should be lround(5.5) = 6");
    require(nearly(live.tap0_gain_db, -3.0F), "tap0_gain_db should be -3");
    require(nearly(live.tap0_phase_rad, 0.25F), "tap0_phase_rad should be 0.25");
    require(nearly(live.los_k_db, 9.0F), "los_k_db should be 9");
    // snr_db=20 against ref_power=1 → noise_power = 0.01 → sigma = sqrt(0.005)
    require(nearly(live.awgn_sigma, std::sqrt(0.005F), 1e-5F),
            "awgn_sigma should derive from snr_db + reference_power");
  }

  // ── Case 2: empty chain — every field defaults to zero ──────────────────
  {
    ocg::ModelConfig m;
    m.id = "empty";
    const ocg::MutableParams live =
        ocg::populate_mutable_params_from_yaml(m, /*reference_power=*/0.0,
                                               /*sample_rate_hz=*/0);
    require(live.path_loss_db == 0.0F, "empty chain → path_loss_db zero");
    require(live.cfo_hz == 0.0F, "empty chain → cfo_hz zero");
    require(live.awgn_sigma == 0.0F, "empty chain → awgn_sigma zero");
    require(live.tap0_delay_samples == 0, "empty chain → tap0_delay_samples zero");
    require(live.tap0_gain_db == 0.0F, "empty chain → tap0_gain_db zero");
    require(live.tap0_phase_rad == 0.0F, "empty chain → tap0_phase_rad zero");
    require(live.los_k_db == 0.0F, "empty chain → los_k_db zero");
  }

  // ── Case 3: leading non-tdl step → tap0 params stay at default ──────────
  {
    ocg::ModelConfig m;
    m.id = "no-leading-tdl";
    m.chain.push_back(make_path_loss_step(/*db=*/7.0));
    m.chain.push_back(make_tdl_step(/*delay=*/2.0, /*gain_db=*/1.0,
                                    /*phase_rad=*/0.1, /*los_k_db=*/3.0));
    const ocg::MutableParams live =
        ocg::populate_mutable_params_from_yaml(m, /*reference_power=*/0.0,
                                               /*sample_rate_hz=*/0);
    require(nearly(live.path_loss_db, 7.0F), "non-leading-tdl chain still picks up path_loss");
    require(live.tap0_delay_samples == 0, "tap params untouched when tdl is not leading");
    require(live.tap0_gain_db == 0.0F, "tap params untouched when tdl is not leading");
    require(live.los_k_db == 0.0F, "los params untouched when tdl is not leading");
  }

  // ── Case 4: POD size contract ───────────────────────────────────────────
  {
    static_assert(sizeof(ocg::MutableParams) == 32,
                  "MutableParams size locked at 32 bytes for the control-plane "
                  "binary copy contract; field changes must update the assert.");
  }

  std::cout << "test_mutable_params OK\n";
  return 0;
}
