# Task 03: Metrics Collection Infrastructure

## Priority: P0 (Blocking)
## Depends on: 01-project-setup
## Blocks: 04-cpu-metrics, 05-memory-metrics, 06-gpu-metrics, 07-network-metrics, 08-disk-metrics

## Description

Build the core metrics infrastructure in `metrics/mod.rs`: the `MetricsSnapshot` data structure, the `MetricsCollector` trait, the background collection thread, and the channel-based communication to the UI thread.

## Acceptance Criteria

- [ ] `MetricsSnapshot` struct defined with fields for all metric categories:
  ```rust
  struct MetricsSnapshot {
      timestamp: Instant,
      cpu: CpuMetrics,
      memory: MemoryMetrics,
      gpu: Option<GpuMetrics>,
      network: NetworkMetrics,
      disk: DiskMetrics,
  }
  ```
- [ ] Individual metric structs defined (in their respective modules):
  - `CpuMetrics { total_usage: f32, per_core_usage: Vec<f32>, frequency_ghz: f32 }`
  - `MemoryMetrics { used_bytes: u64, total_bytes: u64, usage_percent: f32 }`
  - `GpuMetrics { usage_percent: f32, temperature_celsius: f32, vram_used_bytes: u64, vram_total_bytes: u64 }`
  - `NetworkMetrics { upload_bytes_per_sec: u64, download_bytes_per_sec: u64 }`
  - `DiskMetrics { read_bytes_per_sec: u64, write_bytes_per_sec: u64 }`
- [ ] `MetricsCollector` trait defined:
  ```rust
  trait MetricsCollector: Send {
      fn collect(&mut self) -> MetricsSnapshot;
  }
  ```
- [ ] Background thread implementation:
  - Spawns a thread that runs a collection loop at the configured polling interval
  - Uses `std::sync::mpsc` (or `crossbeam-channel`) to send `MetricsSnapshot` to UI
  - Reuses `sysinfo::System` instance across ticks (no re-creation)
  - Supports graceful shutdown (e.g., via atomic bool or channel drop)
- [ ] `MetricsReceiver` wrapper for the UI side:
  - `fn latest(&self) -> Option<MetricsSnapshot>` — drains channel, returns most recent
  - Non-blocking: never stalls the render loop
- [ ] Configurable polling interval (passed from config)

## Testing

- [ ] Unit test: `MetricsSnapshot` can be created and fields accessed
- [ ] Unit test: mock collector sends snapshots through channel correctly
- [ ] Unit test: `latest()` returns the most recent snapshot when multiple are buffered
- [ ] Unit test: graceful shutdown stops the collector thread
- [ ] Trait is mockable via `mockall` for downstream tests

## Notes

- The `sysinfo::System` instance must be created once and refreshed each tick — this is critical for accurate delta-based metrics (CPU usage, network speeds)
- GPU metrics are `Option<GpuMetrics>` since not all systems have a supported GPU
- Consider using `std::sync::mpsc::sync_channel(1)` to bound buffer size, or drain-to-latest pattern
