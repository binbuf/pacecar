# Task 07: Network Metrics Collection

## Priority: P1
## Depends on: 03-metrics-infrastructure
## Blocks: 13-integration

## Description

Implement network metrics collection in `metrics/network.rs` using the `sysinfo` crate. Calculate upload and download speeds by computing deltas between polling ticks.

## Acceptance Criteria

- [ ] `NetworkMetrics` struct:
  - `upload_bytes_per_sec: u64` — current upload speed
  - `download_bytes_per_sec: u64` — current download speed
- [ ] Collection function: `fn collect_network(system: &mut System, prev: &Option<NetworkState>) -> (NetworkMetrics, NetworkState)`
- [ ] Speed calculation:
  - Track previous total bytes sent/received
  - Delta = current total - previous total
  - Speed = delta / elapsed time since last tick
- [ ] Aggregate across all network interfaces (sum of all interfaces)
- [ ] Handles edge cases:
  - First tick (no previous data) → report 0
  - Counter reset/overflow → clamp to 0 instead of negative
  - No network interfaces → report 0

## Testing

- [ ] Unit test: speed correctly calculated from two consecutive readings
- [ ] Unit test: first tick returns zeros
- [ ] Unit test: counter reset handled gracefully
- [ ] Property test: speeds are always non-negative

## Notes

- `sysinfo` provides cumulative bytes sent/received per interface via `Networks`
- Must call `refresh_networks()` (or appropriate refresh) each tick
- The collector must maintain previous state between ticks for delta calculation
- UI layer handles formatting (bytes/sec to KB/s or MB/s)
