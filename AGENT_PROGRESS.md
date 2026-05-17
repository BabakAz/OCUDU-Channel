# Progress

**This file is the live-state record for the workspace. It holds repository state, workspace artifacts, completed changes, blockers, and the next resume point. It shall not define workflow policy, duplicate the reusable playbook, or restate or modify the mission. It is factual, current, and stepwise.**

## Repository State

- Branch: `main`.
- Initial repository contents before setup: `.git` only.
- Current uncommitted workspace edits: none after the CUDA MVP checkpoint commit and push.
- Current ignored local artifacts: `.config`, `build/`, `build-release/`, and `build-review/`.
- Remote: `origin` points to `https://github.com/zhouyou-gu/ocudu-gpu-channel.git`, with setup pushed to `origin/main`.
- Current ignored local config: `.config` contains the GPU workstation connection settings.
- Local environment note: the requested `agent-files` skill was installed under `/Users/charles_gu/.codex/skills/agent-files`.
- Remote workspace: `/home/zhouyou/ocudu-gpu-channel-workspace` exists on `zhouyou@10.34.23.184`.
- Remote project clone: `/home/zhouyou/ocudu-gpu-channel-workspace/ocudu-gpu-channel` exists and currently contains an rsynced copy of the local uncommitted scaffold for GPU validation.
- Remote OCUDU checkout: `/home/zhouyou/ocudu-gpu-channel-workspace/ocudu` exists as a shallow upstream clone.
- Remote validation directories: `builds/`, `configs/`, `results/`, `datasets/`, and `tmp/` exist under the remote workspace.

## Workspace Artifacts

- Control files: `AGENT.md`, `AGENT_GOAL.md`, `AGENT_HARNESS.md`, `AGENT_PROGRESS.md`.
- Local configuration: `.gitignore`, tracked `.config.example`, and ignored `.config`.
- Application scaffold: `CMakeLists.txt`, `apps/`, `include/`, `src/`, `tests/`, `examples/`, and `docs/` exist locally as uncommitted implementation work.
- Local implementation includes C++20/CMake build plumbing, optional CUDA detection, libzmq integration, CPU and CUDA MVP channel processing, broker/runtime CLIs, synthetic ZMQ tools, config examples, docs, and regression tests.
- Remote helpers: `scripts/remote/` contains local scripts for sourcing `.config`, initializing the remote workspace, probing dependencies, and syncing a pushed branch.
- Remote user-space tools: `/home/zhouyou/ocudu-gpu-channel-workspace/tools/env.sh` exists and points to user-space CMake 3.31.12, CUDA Toolkit 12.8.1, and ZeroMQ 4.3.5.
- Generated build artifacts: local `build/`, `build-release/`, and `build-review/` are ignored.

## Completed Changes

<!--
Progress entry template
- [Category] <factual completed change>
-->

- [Setup] Installed the requested `agent-files` skill from `zhouyou-gu/skill-marketplace-template`.
- [Setup] Created the four-file agent contract in the repository.
- [Mission] Created `AGENT_GOAL.md` with the user-approved multi-gNB and multi-UE channel-emulation mission.
- [Harness] Recorded durable preferences for source-backed OCUDU/ZMQ claims and topology-aware design.
- [Config] Added `.gitignore` to keep local `.config` out of Git.
- [Config] Added `.config.example` with placeholder remote workstation variables.
- [Config] Added ignored `.config` with the current GPU workstation connection settings.
- [Harness] Recorded the durable convention that private workstation details belong in ignored `.config` and placeholders belong in `.config.example`.
- [Validation] Verified `git status --short --ignored` reports `.config` only as ignored and verified `.config.example` contains no private workstation values.
- [Git] Committed and pushed the initial project setup to `origin/main`.
- [Structure] Updated `.gitignore` to ignore build outputs, raw run artifacts, local datasets/tmp, logs, pcaps, and local external OCUDU checkouts.
- [Structure] Added `docs/project-structure.md` documenting the canonical local repo layout and remote RTX workstation workspace layout.
- [Structure] Added `scripts/remote/` helpers for remote config loading, workspace initialization, dependency probing, and pushed-branch syncing.
- [Structure] Created the remote workspace directory tree under `/home/zhouyou/ocudu-gpu-channel-workspace`.
- [Structure] Cloned this repository to the remote workspace project directory.
- [Structure] Cloned OCUDU upstream to the remote workspace sibling `ocudu` directory.
- [Harness] Recorded the durable convention that local Git is canonical and the remote workstation is a validation mirror.
- [Validation] Verified local Release CMake configure/build and `ctest` pass with 2/2 tests.
- [Validation] Verified local `build/` and `build-release/` are ignored and not intended for commit.
- [Validation] Verified remote project clone is clean and remote validation directories exist.
- [Implementation] Added the initial C++20/CMake implementation scaffold with libzmq dependency discovery and optional CUDA build detection.
- [Implementation] Added public headers and sources for IQ sample handling, topology config parsing, bounded rings, latency statistics, CPU channel processing, and broker runtime state.
- [Implementation] Added CLI entrypoints for the channel broker, model-mix benchmark, and synthetic OCUDU-style ZMQ source/sink tools.
- [Implementation] Added example topology and documentation for usage, feasibility, distributed networking, release roadmap, and project structure.
- [Review Fix] Refactored the broker to use explicit ZMQ REQ/REP socket state with nonblocking `zmq_poll` handling instead of serial blocking request loops.
- [Review Fix] Replaced broker latest-packet replay behavior with per-device source rings, per-link cursors, sequence-gap detection, bounded backpressure, and visible starvation/overflow/error counters.
- [Review Fix] Added preallocated broker and CPU processor buffers for fixed-size batch processing, while keeping convenience mix APIs outside the broker hot path.
- [Review Fix] Hardened config parsing and validation to fail on unknown runtime/device/link keys, unknown model-step parameters, invalid roles, empty model chains, and negative noise or delay values.
- [Review Fix] Clarified benchmark output as `model_mix_latency` so it is not mistaken for full ZMQ broker latency.
- [Tests] Added regression coverage for config validation, bounded ring overflow/discard behavior, CPU gain/path processing, and delay continuity.
- [Validation] Verified local Release build and `ctest` pass with 3/3 tests after review fixes.
- [Validation] Ran `ocudu-gpu-channel-bench` on `examples/topology.local.yaml` for 1 second; CPU backend reported a green `model_mix_latency` run.
- [Validation] Ran the broker with synthetic ZMQ source/sink tools and verified real sample flow, bounded backpressure, no TX queue overflows, no TX sequence gaps, and no ZMQ errors.
- [Validation] Verified `git diff --check` passes after the review fixes.
- [Review] Reviewed the current C++/CMake implementation for ZMQ broker behavior, config validation, channel processing, benchmark behavior, synthetic tools, and remote readiness.
- [Validation] Configured a clean local `build-review/` Release build; CUDA compiler was not found locally and libzmq 4.3.5 was found through pkg-config.
- [Validation] Built `build-review/` successfully and verified `ctest --test-dir build-review --output-on-failure` passes with 3/3 tests.
- [Validation] Ran `ocudu-gpu-channel-bench` from `build-review/` for 1 second on `examples/topology.local.yaml`; CPU backend reported green `model_mix_latency`.
- [Validation] Ran a local two-device synthetic ZMQ loop from `build-review/`; both sinks received samples, and broker counters ended with zero TX queue overflows, zero TX sequence gaps, and zero ZMQ errors.
- [Validation] Verified local `backend: cuda` config fails fast on this non-CUDA build.
- [Validation] Found that numeric config values with trailing garbage are currently accepted by the parser.
- [Validation] Found that source/destination sample-rate mismatches are currently accepted even though no resampler exists.
- [Validation] Re-ran the remote workstation probe; RTX 5090 remains visible, while `cmake`, `nvcc`, and `iperf3` are still missing.
- [Config] Made numeric parsing strict so trailing characters in numeric values are rejected.
- [Config] Added validation that rejects mixed sample-rate links until an explicit resampler exists.
- [Backend] Added a shared `ChannelProcessor` interface and backend factory used by the broker and benchmark.
- [Backend] Added CUDA support validation that accepts only gain, path loss, phase, and CFO for the CUDA MVP and rejects AWGN and delay steps.
- [CUDA] Implemented a CUDA MVP processor with per-link streams, pinned host staging buffers, device buffers, CUDA events, and gain/path loss/phase/CFO kernel support.
- [Benchmark] Reworked the benchmark to use the selected backend and emit CUDA `h2d_us`, `kernel_us`, `d2h_us`, and `gpu_process_us` rows.
- [Runtime] Added `--strict-realtime` to the broker CLI so starvation, queue overflow, sequence gaps, or ZMQ errors fail validation runs.
- [Tools] Added `--request-interval-us` pacing to the synthetic sink for realistic strict realtime batch request cadence.
- [Examples] Added `examples/topology.cuda-mvp.yaml` for the 23.04 MS/s CUDA MVP model chain.
- [Remote] Added user-space remote bootstrap and build/benchmark helper scripts for CMake 3.31.12, CUDA Toolkit 12.8.1, and optional ZeroMQ 4.3.5.
- [Remote] Bootstrapped the RTX workstation user-space toolchain under `/home/zhouyou/ocudu-gpu-channel-workspace/tools`.
- [Remote] Rsynced the local uncommitted worktree to the remote project clone for validation without committing or pushing.
- [Validation] Verified local Release build and `ctest --test-dir build-review --output-on-failure` pass with 3/3 tests after CUDA MVP changes.
- [Validation] Verified `examples/topology.cuda-mvp.yaml` fails fast on the local non-CUDA build.
- [Validation] Built remote CPU and CUDA release trees with the user-space toolchain; both remote `ctest` runs passed 3/3 tests.
- [Validation] Ran the remote 23.04 MS/s CPU benchmark; `model_mix_latency` p99 was 15.388 us with a green gate.
- [Validation] Ran the remote 23.04 MS/s CUDA MVP benchmark; `model_mix_latency` p99 was 72.756 us with a green gate, with CUDA timing rows for H2D, kernel, D2H, and full GPU process time.
- [Validation] Ran a strict remote CUDA synthetic ZMQ loop with paced sinks; broker exited successfully with zero RX starvation, zero TX queue overflows, zero TX sequence gaps, and zero ZMQ errors.
- [Validation] Verified `git diff --check` passes after CUDA MVP changes.
- [Config] Changed the default runtime backend from CPU to CUDA so configs without an explicit backend target GPU channel emulation.
- [Docs] Reframed README, feasibility notes, and release roadmap to state CUDA/GPU channel emulation as the primary target and CPU as reference/baseline/local-development support.
- [Harness] Recorded the durable rule that CUDA/GPU channel emulation is the primary product target and CPU should be described only as reference, baseline, local development, or unported-model fallback.
- [Validation] Verified local `build-review` build and `ctest` still pass 3/3 after making CUDA the default backend.
- [Validation] Rebuilt the remote CUDA release tree and verified remote CUDA `ctest` still passes 3/3.
- [Validation] Re-ran the remote CUDA MVP benchmark after the GPU-target framing change; `model_mix_latency` p99 was 73.375 us with a green gate.
- [Review] Continued the CUDA MVP implementation review across broker socket-state handling, CUDA backend allocation/timing behavior, config validation, benchmark semantics, and remote helper usage.
- [Validation] Re-ran local `build-review` build and `ctest --test-dir build-review --output-on-failure`; all 3 tests passed.
- [Validation] Re-ran the local CPU benchmark on `examples/topology.local.yaml` for 1 second; `model_mix_latency` p99 was 59.500 us with a green gate.
- [Validation] Re-verified `examples/topology.cuda-mvp.yaml` fails fast on the local non-CUDA build.
- [Validation] Re-ran remote CUDA `ctest`; all 3 tests passed.
- [Validation] Re-ran the remote CUDA MVP benchmark on the RTX workstation for 1 second; `model_mix_latency` p99 was 72.824 us with a green gate.
- [Review Fix] Changed broker ZMQ EFSM and data-path exceptions from hidden per-socket errors into fatal run errors so REQ/REP sockets are not reused after an unrecoverable state transition.
- [Review Fix] Added RAII cleanup for the broker ZMQ context so sockets are destroyed before the context on normal and exceptional exits.
- [Review Fix] Moved broker poll vectors out of the loop and added preallocated TX receive buffers to avoid recurring vector and IQ-buffer allocation in the broker loop.
- [Tests] Added CUDA-build processing coverage that compares CUDA output against the CPU reference for gain, path loss, phase, and CFO across two batches.
- [Remote] Fixed `bootstrap-user-tools.sh` so generated `tools/env.sh` includes user-space ZeroMQ paths only when that directory exists.
- [Harness] Recorded that `.config` should be loaded through `scripts/remote/common.sh`, not sourced directly, to avoid local tilde expansion.
- [Validation] Verified local `cmake --build build-review -j4` and `ctest --test-dir build-review --output-on-failure` pass with 3/3 tests after broker and CUDA test fixes.
- [Validation] Re-ran the local CPU benchmark on `examples/topology.local.yaml` for 1 second; `model_mix_latency` p99 was 77.583 us with a green gate.
- [Validation] Re-verified `examples/topology.cuda-mvp.yaml` fails fast on the local non-CUDA build after the fixes.
- [Validation] Ran a local strict synthetic ZMQ loop after the broker fixes; broker exited with zero RX starvation, zero TX queue overflows, zero TX sequence gaps, and zero ZMQ errors.
- [Remote] Rsynced the fixed local uncommitted worktree to the remote project clone for CUDA validation.
- [Validation] Rebuilt the remote CUDA release tree and verified remote CUDA `ctest` passes 3/3 with the new CUDA-vs-CPU correctness check.
- [Validation] Re-ran the remote CUDA MVP benchmark on the RTX workstation for 1 second; `model_mix_latency` p99 was 72.905 us with a green gate.
- [Validation] Ran a strict remote CUDA synthetic ZMQ loop after the broker fixes; broker exited with zero RX starvation, zero TX queue overflows, zero TX sequence gaps, and zero ZMQ errors.
- [Git] Committed and pushed the CUDA MVP project checkpoint to `origin/main`.

## Blockers and Risks

- Remote dependency gap: `iperf3` is still missing on the RTX workstation under the user-space-only constraint.
- Remote GPU driver and CUDA toolkit are visible through `nvidia-smi` and user-space `nvcc`.
- Remote active network path is Wi-Fi (`wlp129s0`), which is suitable for SSH/control but not accepted as a real-time distributed IQ data path.
- Public documentation establishes OCUDU ZMQ support, but this repository does not yet contain runtime tests against OCUDU ZMQ endpoints.
- CUDA MVP intentionally does not support AWGN, integer delay, or fractional delay; those model steps fail fast on `backend: cuda`.
- Broker hot path no longer allocates poll vectors or TX receive IQ buffers in each loop, but still constructs link-key strings and uses unordered-map lookups while processing RX requests.
- CUDA MVP currently synchronizes each link inside `process_into`, so per-link streams are allocated but not yet overlapped in the broker/benchmark path.

## Next Resume Point

- Continue from the pushed CUDA MVP checkpoint by deciding whether to address remaining broker link-key/map hot-path work, CUDA stream overlap, or OCUDU runtime interop next.
