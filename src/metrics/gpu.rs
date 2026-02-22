/// GPU metrics: usage, temperature, and VRAM.
/// Wrapped in `Option` at the snapshot level since not all systems have a supported GPU.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct GpuMetrics {
    pub usage_percent: f32,
    pub temperature_celsius: f32,
    pub vram_used_bytes: u64,
    pub vram_total_bytes: u64,
}
