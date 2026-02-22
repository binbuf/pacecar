# Task 04: CPU Metrics Collection

## Priority: P1
## Depends on: 03-metrics-infrastructure
## Blocks: 13-integration

## Description

Implement CPU metrics collection in `metrics/cpu.rs` using the `sysinfo` crate. Collect total CPU usage, per-core usage, and CPU frequency.

## Acceptance Criteria

- [ ] `CpuMetrics` struct populated from `sysinfo::System`:
  - `total_usage: f32` — overall CPU usage percentage (0.0–100.0)
  - `per_core_usage: Vec<f32>` — per-logical-core usage percentages
  - `frequency_ghz: f32` — current CPU frequency in GHz
- [ ] Collection function: `fn collect_cpu(system: &mut System) -> CpuMetrics`
- [ ] Correct `sysinfo` refresh calls:
  - `refresh_cpu_all()` or equivalent for CPU usage
  - Proper handling of the first-tick issue (sysinfo needs two refreshes for accurate CPU %)
- [ ] Frequency reported in GHz (converted from MHz if needed)
- [ ] Handles edge cases:
  - Systems with a single core
  - Frequency unavailable (report 0.0 or N/A)

## Testing

- [ ] Unit test with mocked/trait-abstracted system data
- [ ] Property test: CPU usage values always in 0.0–100.0 range
- [ ] Property test: frequency always non-negative

## Notes

- `sysinfo` reports CPU frequency in MHz — convert to GHz by dividing by 1000.0
- The first call to `cpu_usage()` after creating a `System` always returns 0 — the collector should handle this gracefully (e.g., skip the first snapshot or document the warm-up)
- `global_cpu_usage()` gives total; iterating `cpus()` gives per-core
