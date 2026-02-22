/// Disk I/O metrics: read and write speeds.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct DiskMetrics {
    pub read_bytes_per_sec: u64,
    pub write_bytes_per_sec: u64,
}
