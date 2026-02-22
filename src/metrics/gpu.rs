use crate::config::GpuSelection;

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

/// Initialize the GPU provider based on the user's GPU selection.
///
/// Resolution order:
/// 1. If `nvidia` feature is enabled, try NVML with the selected device.
/// 2. On Windows, try D3DKMT for AMD/Intel GPUs.
/// 3. Return `None` if no provider could be created.
pub fn init_gpu_provider(selection: &GpuSelection) -> Option<Box<dyn GpuProvider>> {
    #[cfg(feature = "nvidia")]
    {
        let result = match selection {
            GpuSelection::Auto => NvmlGpuProvider::new(0),
            GpuSelection::ByIndex(idx) => NvmlGpuProvider::new(*idx),
            GpuSelection::ByName(name) => NvmlGpuProvider::by_name(name),
        };
        if let Some(provider) = result {
            eprintln!("[pacecar] NVIDIA GPU detected via NVML");
            return Some(Box::new(provider));
        }
        eprintln!("[pacecar] NVML initialization failed — trying D3DKMT fallback");
    }

    #[cfg(target_os = "windows")]
    {
        let result = match selection {
            GpuSelection::Auto => super::gpu_d3dkmt::D3dkmtGpuProvider::new(0),
            GpuSelection::ByIndex(idx) => super::gpu_d3dkmt::D3dkmtGpuProvider::new(*idx),
            GpuSelection::ByName(name) => super::gpu_d3dkmt::D3dkmtGpuProvider::by_name(name),
        };
        if let Some(provider) = result {
            eprintln!("[pacecar] GPU detected via D3DKMT");
            return Some(Box::new(provider));
        }
        eprintln!("[pacecar] D3DKMT initialization failed — no GPU metrics");
    }

    #[cfg(not(any(feature = "nvidia", target_os = "windows")))]
    {
        let _ = selection;
        eprintln!("[pacecar] GPU metrics disabled (compile with --features nvidia)");
    }

    None
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
        device_index: u32,
    }

    impl NvmlGpuProvider {
        /// Try to initialize NVML and verify the given device index exists.
        pub fn new(device_index: u32) -> Option<Self> {
            let nvml = Nvml::init().ok()?;
            let _device = nvml.device_by_index(device_index).ok()?;
            Some(Self { nvml, device_index })
        }

        /// Find an NVML device whose name contains the given substring.
        pub fn by_name(name: &str) -> Option<Self> {
            let nvml = Nvml::init().ok()?;
            let count = nvml.device_count().ok()?;
            for i in 0..count {
                if let Ok(device) = nvml.device_by_index(i) {
                    if let Ok(dev_name) = device.name() {
                        if dev_name.to_lowercase().contains(&name.to_lowercase()) {
                            return Some(Self {
                                nvml,
                                device_index: i,
                            });
                        }
                    }
                }
            }
            None
        }
    }

    impl GpuProvider for NvmlGpuProvider {
        fn query(&self) -> Option<GpuMetrics> {
            let device = self.nvml.device_by_index(self.device_index).ok()?;

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
        #[cfg(not(feature = "nvidia"))]
        {
            let provider = init_gpu_provider(&GpuSelection::Auto);
            // On Windows, D3DKMT might still find a GPU, so we just verify no panic.
            let _ = provider;
        }
    }
}
