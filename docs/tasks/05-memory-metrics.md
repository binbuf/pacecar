# Task 05: Memory (RAM) Metrics Collection

## Priority: P1
## Depends on: 03-metrics-infrastructure
## Blocks: 13-integration

## Description

Implement RAM metrics collection in `metrics/memory.rs` using the `sysinfo` crate. Collect used memory, total memory, and usage percentage.

## Acceptance Criteria

- [ ] `MemoryMetrics` struct populated from `sysinfo::System`:
  - `used_bytes: u64` — currently used RAM in bytes
  - `total_bytes: u64` — total installed RAM in bytes
  - `usage_percent: f32` — calculated as `(used / total) * 100.0`
- [ ] Collection function: `fn collect_memory(system: &mut System) -> MemoryMetrics`
- [ ] Correct `sysinfo` refresh call: `refresh_memory()`
- [ ] Handles edge cases:
  - `total_bytes == 0` (avoid division by zero, report 0%)

## Testing

- [ ] Unit test with known values verifying percentage calculation
- [ ] Unit test: division by zero safety when total is 0
- [ ] Property test: usage_percent always in 0.0–100.0

## Notes

- `sysinfo` reports memory in bytes
- UI layer will handle formatting (bytes to GB display) — this module just provides raw bytes
- This is one of the simpler metrics to implement; good candidate for first implementation
