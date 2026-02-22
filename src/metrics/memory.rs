/// RAM metrics: used/total bytes and usage percentage.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct MemoryMetrics {
    pub used_bytes: u64,
    pub total_bytes: u64,
    pub usage_percent: f32,
}
