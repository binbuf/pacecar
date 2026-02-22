# Task 06: GPU Metrics Collection

## Priority: P2
## Depends on: 03-metrics-infrastructure
## Blocks: 13-integration

## Description

Implement GPU metrics collection in `metrics/gpu.rs`. Primary path uses `nvml-wrapper` for NVIDIA GPUs. The entire GPU module is optional — systems without supported GPUs should gracefully return `None`.

## Acceptance Criteria

- [ ] `GpuMetrics` struct:
  - `usage_percent: f32` — GPU utilization (0.0–100.0)
  - `temperature_celsius: f32` — GPU temperature
  - `vram_used_bytes: u64` — used VRAM
  - `vram_total_bytes: u64` — total VRAM
- [ ] NVIDIA path via `nvml-wrapper`:
  - Initialize NVML once at collector startup
  - Query first GPU device (index 0) each tick
  - Collect utilization rates, temperature, and memory info
- [ ] Graceful fallback:
  - If NVML initialization fails (no NVIDIA GPU / no driver) → return `None`
  - If any individual query fails → use last known value or 0
- [ ] Collection function: `fn collect_gpu(nvml: &Option<Nvml>) -> Option<GpuMetrics>`
- [ ] Feature-gated behind `nvidia` cargo feature (optional dependency)

## Testing

- [ ] Unit test: `None` returned when NVML is unavailable
- [ ] Unit test: valid metrics returned from mocked NVML interface
- [ ] Unit test: partial failures return sensible defaults

## Notes

- `nvml-wrapper` requires NVIDIA drivers to be installed; CI may not have them
- Abstract NVML calls behind a trait for testability
- Future: AMD GPU support via ROCm/ADL (post-MVP)
- Future: D3DKMT fallback for basic GPU metrics on any Windows GPU (post-MVP)
- Consider logging GPU detection results at startup for debugging
