# Feasibility Study Plan

The project must prove that the emulator's processing delay does not destabilize the OCUDU runtime. Processing delay is separate from modeled channel delay: modeled propagation delay may be intentionally inserted into samples, while processing delay is overhead added by the emulator itself.

## Baseline Measurements

Measure each topology at 23.04, 30.72, and 61.44 MS/s:

- raw ZMQ request/reply pass-through latency;
- CPU reference channel-model latency for comparison only;
- CUDA H2D, kernel, D2H, and full pipeline latency for the project target path;
- full broker added latency;
- queue depth, starvation count, late/overflow events, and sustained throughput.

## Gates

Use the configured subcarrier spacing to derive slot duration:

- 15 kHz: 1000 us;
- 30 kHz: 500 us;
- 60 kHz: 250 us;
- 120 kHz: 125 us.

Classify a run as:

- green when p99 added emulator latency is at most 25% of slot duration and the run has no drift or overflow;
- yellow when stable but p99 is at most one slot;
- red when p99 exceeds one slot or queue growth, missed deadline, RX starvation, TX overflow, or OCUDU late events recur.

## GPU Risk Controls

GPU channel emulation is the project target. The CPU path is retained as a reference/baseline and for models not yet ported to CUDA. GPU acceleration is only useful if model compute amortizes ZMQ, host-to-device transfer, kernel launch, and device-to-host transfer; pass-through and simple gain may remain faster on CPU for small batches, but those CPU results are not GPU scale claims.

The GPU data path must use:

- pinned host buffers;
- preallocated device buffers;
- non-default CUDA streams;
- asynchronous copies;
- no allocation in the hot path;
- CUDA Graphs only after batch shapes are stable.

## Publication Rule

Publish measured envelopes, not unmeasured scale claims. Every table must name the hardware, sample rate, topology, model chain, backend, CPU affinity/tuning profile, and run duration.
