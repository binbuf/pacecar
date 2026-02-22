/// CPU metrics: total usage, per-core usage, and frequency.
#[derive(Debug, Clone, PartialEq)]
pub struct CpuMetrics {
    pub total_usage: f32,
    pub per_core_usage: Vec<f32>,
    pub frequency_ghz: f32,
}

impl Default for CpuMetrics {
    fn default() -> Self {
        Self {
            total_usage: 0.0,
            per_core_usage: Vec::new(),
            frequency_ghz: 0.0,
        }
    }
}
