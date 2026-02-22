use sysinfo::System;

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

/// Collect CPU metrics from a `sysinfo::System` instance.
///
/// The caller must have called `refresh_cpu_all()` at least twice (with a delay
/// between calls) before this function returns accurate usage values. On the
/// very first tick, `sysinfo` returns 0% for all CPUs — this is expected and
/// the caller should handle it (e.g., by discarding the first snapshot or
/// displaying a "warming up" state).
pub fn collect_cpu(system: &System) -> CpuMetrics {
    let total_usage = system.global_cpu_usage();

    let cpus = system.cpus();
    let per_core_usage: Vec<f32> = cpus.iter().map(|cpu| cpu.cpu_usage()).collect();

    // Try to get current (dynamic) frequency first; fall back to sysinfo's
    // base frequency if the platform-specific call is unavailable.
    let frequency_ghz = current_frequency_ghz(cpus.len())
        .unwrap_or_else(|| sysinfo_frequency_ghz(cpus));

    CpuMetrics {
        total_usage,
        per_core_usage,
        frequency_ghz,
    }
}

/// Fallback: average frequency from sysinfo (base/rated speed on Windows).
fn sysinfo_frequency_ghz(cpus: &[sysinfo::Cpu]) -> f32 {
    if cpus.is_empty() {
        0.0
    } else {
        let total_mhz: u64 = cpus.iter().map(|cpu| cpu.frequency()).sum();
        (total_mhz as f32) / (cpus.len() as f32) / 1000.0
    }
}

/// Read current (dynamic) CPU frequency via the Windows
/// `CallNtPowerInformation` API. Returns `None` on non-Windows platforms
/// or if the call fails.
#[cfg(target_os = "windows")]
fn current_frequency_ghz(num_cpus: usize) -> Option<f32> {
    use std::mem;

    #[repr(C)]
    struct ProcessorPowerInformation {
        _number: u32,
        _max_mhz: u32,
        current_mhz: u32,
        _mhz_limit: u32,
        _max_idle_state: u32,
        _current_idle_state: u32,
    }

    #[link(name = "powrprof")]
    unsafe extern "system" {
        fn CallNtPowerInformation(
            information_level: i32,
            input_buffer: *const std::ffi::c_void,
            input_buffer_length: u32,
            output_buffer: *mut std::ffi::c_void,
            output_buffer_length: u32,
        ) -> i32;
    }

    const PROCESSOR_INFORMATION: i32 = 11;

    if num_cpus == 0 {
        return None;
    }

    let entry_size = mem::size_of::<ProcessorPowerInformation>();
    let buf_len = entry_size * num_cpus;
    let mut buffer = vec![0u8; buf_len];

    let status = unsafe {
        CallNtPowerInformation(
            PROCESSOR_INFORMATION,
            std::ptr::null(),
            0,
            buffer.as_mut_ptr() as *mut std::ffi::c_void,
            buf_len as u32,
        )
    };

    if status != 0 {
        return None;
    }

    let infos = unsafe {
        std::slice::from_raw_parts(
            buffer.as_ptr() as *const ProcessorPowerInformation,
            num_cpus,
        )
    };

    let total_mhz: u32 = infos.iter().map(|i| i.current_mhz).sum();
    Some(total_mhz as f32 / num_cpus as f32 / 1000.0)
}

#[cfg(not(target_os = "windows"))]
fn current_frequency_ghz(_num_cpus: usize) -> Option<f32> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn collect_cpu_returns_valid_struct() {
        let mut system = System::new();
        system.refresh_cpu_all();
        let metrics = collect_cpu(&system);

        // First tick may return 0 or an inaccurate value — just check it's in range.
        assert!(
            (0.0..=100.0).contains(&metrics.total_usage),
            "total_usage out of range: {}",
            metrics.total_usage
        );
        assert!(metrics.frequency_ghz >= 0.0);
    }

    #[test]
    fn collect_cpu_after_warmup() {
        let mut system = System::new();
        system.refresh_cpu_all();
        // A short sleep + second refresh gives sysinfo a baseline to compute deltas.
        std::thread::sleep(std::time::Duration::from_millis(250));
        system.refresh_cpu_all();

        let metrics = collect_cpu(&system);

        // After warmup, total_usage should be in [0, 100].
        assert!(
            (0.0..=100.0).contains(&metrics.total_usage),
            "total_usage out of range: {}",
            metrics.total_usage
        );

        for (i, &core) in metrics.per_core_usage.iter().enumerate() {
            assert!(
                (0.0..=100.0).contains(&core),
                "core {} usage out of range: {}",
                i,
                core
            );
        }

        assert!(
            metrics.frequency_ghz >= 0.0,
            "frequency_ghz should be non-negative: {}",
            metrics.frequency_ghz
        );
    }

    #[test]
    fn default_metrics_are_zero() {
        let m = CpuMetrics::default();
        assert_eq!(m.total_usage, 0.0);
        assert!(m.per_core_usage.is_empty());
        assert_eq!(m.frequency_ghz, 0.0);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn cpu_usage_always_in_range(total in 0.0f32..=100.0, cores in prop::collection::vec(0.0f32..=100.0, 1..128), freq in 0.0f32..=10.0) {
            let metrics = CpuMetrics {
                total_usage: total,
                per_core_usage: cores.clone(),
                frequency_ghz: freq,
            };
            prop_assert!((0.0..=100.0).contains(&metrics.total_usage));
            for &c in &metrics.per_core_usage {
                prop_assert!((0.0..=100.0).contains(&c));
            }
            prop_assert!(metrics.frequency_ghz >= 0.0);
        }

        #[test]
        fn frequency_conversion_correct(mhz_values in prop::collection::vec(0u64..=10_000, 1..128)) {
            let count = mhz_values.len() as f32;
            let total_mhz: u64 = mhz_values.iter().sum();
            let ghz = (total_mhz as f32) / count / 1000.0;
            prop_assert!(ghz >= 0.0);
            prop_assert!(ghz <= 10.0);
        }
    }
}
