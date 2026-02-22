# Task 08: Disk I/O Metrics Collection

## Priority: P1
## Depends on: 03-metrics-infrastructure
## Blocks: 13-integration

## Description

Implement disk I/O metrics collection in `metrics/disk.rs` using the `sysinfo` crate. Calculate read and write speeds by computing deltas between polling ticks.

## Acceptance Criteria

- [ ] `DiskMetrics` struct:
  - `read_bytes_per_sec: u64` — current read speed
  - `write_bytes_per_sec: u64` — current write speed
- [ ] Collection function: `fn collect_disk(system: &mut System, prev: &Option<DiskState>) -> (DiskMetrics, DiskState)`
- [ ] Speed calculation:
  - Track previous total bytes read/written across all disks
  - Delta = current total - previous total
  - Speed = delta / elapsed time since last tick
- [ ] Aggregate across all disk devices
- [ ] Handles edge cases:
  - First tick (no previous data) → report 0
  - Counter reset → clamp to 0
  - No disks detected → report 0

## Testing

- [ ] Unit test: speed correctly calculated from two consecutive readings
- [ ] Unit test: first tick returns zeros
- [ ] Property test: speeds always non-negative

## Notes

- Check which `sysinfo` API provides disk I/O counters (read/written bytes) vs. disk space info — they are different
- `sysinfo` may use `Disks` for space and process-level I/O; verify the correct API for system-wide I/O throughput
- If `sysinfo` doesn't provide system-wide disk I/O counters, may need `windows-rs` with performance counters as fallback
- UI layer handles formatting (bytes/sec to MB/s)
