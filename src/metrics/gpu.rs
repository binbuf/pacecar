/// GPU metrics: usage, temperature, and VRAM.
/// Wrapped in `Option` at the snapshot level since not all systems have a supported GPU.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct GpuMetrics {
    pub usage_percent: f32,
    pub temperature_celsius: f32,
    pub vram_used_bytes: u64,
    pub vram_total_bytes: u64,
}

/// Trait abstracting GPU queries for testability.
/// The real implementation uses NVML; tests can provide a mock.
#[cfg_attr(test, mockall::automock)]
pub trait GpuProvider: Send {
    /// Query current GPU metrics. Returns `None` if the GPU is unavailable.
    fn query(&self) -> Option<GpuMetrics>;
}

/// Initialize the GPU provider. Returns `None` if no supported GPU is found.
///
/// When the `nvidia` feature is enabled, this attempts to initialize NVML and
/// open device 0. On failure (no NVIDIA GPU, missing driver) it returns `None`.
/// Without the feature, it always returns `None`.
pub fn init_gpu_provider() -> Option<Box<dyn GpuProvider>> {
    #[cfg(feature = "nvidia")]
    {
        match NvmlGpuProvider::new() {
            Some(provider) => {
                eprintln!("[pacecar] NVIDIA GPU detected via NVML");
                Some(Box::new(provider))
            }
            None => {
                eprintln!("[pacecar] NVML initialization failed — no NVIDIA GPU metrics");
                None
            }
        }
    }
    #[cfg(not(feature = "nvidia"))]
    {
        eprintln!("[pacecar] GPU metrics disabled (compile with --features nvidia)");
        None
    }
}

/// Collect GPU metrics from the provider. Returns `None` if provider is `None`
/// or the query fails.
pub fn collect_gpu(provider: &Option<Box<dyn GpuProvider>>) -> Option<GpuMetrics> {
    provider.as_ref().and_then(|p| p.query())
}

// ---------------------------------------------------------------------------
// NVML-backed implementation (feature-gated)
// ---------------------------------------------------------------------------

#[cfg(feature = "nvidia")]
mod nvml_impl {
    use super::*;
    use nvml_wrapper::enum_wrappers::device::TemperatureSensor;
    use nvml_wrapper::Nvml;

    pub struct NvmlGpuProvider {
        nvml: Nvml,
    }

    impl NvmlGpuProvider {
        /// Try to initialize NVML and verify device 0 exists.
        pub fn new() -> Option<Self> {
            let nvml = Nvml::init().ok()?;
            // Verify at least one device is accessible.
            let _device = nvml.device_by_index(0).ok()?;
            Some(Self { nvml })
        }
    }

    impl GpuProvider for NvmlGpuProvider {
        fn query(&self) -> Option<GpuMetrics> {
            let device = self.nvml.device_by_index(0).ok()?;

            let usage_percent = device
                .utilization_rates()
                .map(|u| u.gpu as f32)
                .unwrap_or(0.0);

            let temperature_celsius = device
                .temperature(TemperatureSensor::Gpu)
                .map(|t| t as f32)
                .unwrap_or(0.0);

            let (vram_used_bytes, vram_total_bytes) = device
                .memory_info()
                .map(|m| (m.used, m.total))
                .unwrap_or((0, 0));

            Some(GpuMetrics {
                usage_percent,
                temperature_celsius,
                vram_used_bytes,
                vram_total_bytes,
            })
        }
    }
}

#[cfg(feature = "nvidia")]
pub use nvml_impl::NvmlGpuProvider;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn collect_gpu_returns_none_when_no_provider() {
        let provider: Option<Box<dyn GpuProvider>> = None;
        assert!(collect_gpu(&provider).is_none());
    }

    #[test]
    fn collect_gpu_returns_none_when_provider_returns_none() {
        let mut mock = MockGpuProvider::new();
        mock.expect_query().returning(|| None);

        let provider: Option<Box<dyn GpuProvider>> = Some(Box::new(mock));
        assert!(collect_gpu(&provider).is_none());
    }

    #[test]
    fn collect_gpu_returns_metrics_from_provider() {
        let expected = GpuMetrics {
            usage_percent: 45.0,
            temperature_celsius: 68.0,
            vram_used_bytes: 4_000_000_000,
            vram_total_bytes: 8_000_000_000,
        };
        let expected_clone = expected;

        let mut mock = MockGpuProvider::new();
        mock.expect_query().returning(move || Some(expected_clone));

        let provider: Option<Box<dyn GpuProvider>> = Some(Box::new(mock));
        let result = collect_gpu(&provider).unwrap();
        assert_eq!(result, expected);
    }

    #[test]
    fn collect_gpu_partial_failure_returns_defaults() {
        // Simulate a provider that returns some fields as 0 (individual query failures).
        let mut mock = MockGpuProvider::new();
        mock.expect_query().returning(|| {
            Some(GpuMetrics {
                usage_percent: 30.0,
                temperature_celsius: 0.0, // temp query failed
                vram_used_bytes: 0,       // memory query failed
                vram_total_bytes: 0,
            })
        });

        let provider: Option<Box<dyn GpuProvider>> = Some(Box::new(mock));
        let result = collect_gpu(&provider).unwrap();
        assert_eq!(result.usage_percent, 30.0);
        assert_eq!(result.temperature_celsius, 0.0);
        assert_eq!(result.vram_used_bytes, 0);
        assert_eq!(result.vram_total_bytes, 0);
    }

    #[test]
    fn default_gpu_metrics_are_zero() {
        let m = GpuMetrics::default();
        assert_eq!(m.usage_percent, 0.0);
        assert_eq!(m.temperature_celsius, 0.0);
        assert_eq!(m.vram_used_bytes, 0);
        assert_eq!(m.vram_total_bytes, 0);
    }

    #[test]
    fn init_gpu_provider_returns_none_without_nvidia_feature() {
        // Without the `nvidia` feature, init should always return None.
        // This test only asserts the non-feature path; the NVML path
        // depends on actual hardware/drivers.
        #[cfg(not(feature = "nvidia"))]
        {
            let provider = init_gpu_provider();
            assert!(provider.is_none());
        }
    }
}
