/// Network metrics: upload and download speeds.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct NetworkMetrics {
    pub upload_bytes_per_sec: u64,
    pub download_bytes_per_sec: u64,
}
