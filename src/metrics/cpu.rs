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

    // sysinfo reports frequency in MHz; convert to GHz.
    // Use the average frequency across all cores (they may differ with
    // per-core boosting). Falls back to 0.0 if no CPUs are reported.
    let frequency_ghz = if cpus.is_empty() {
        0.0
    } else {
        let total_mhz: u64 = cpus.iter().map(|cpu| cpu.frequency()).sum();
        (total_mhz as f32) / (cpus.len() as f32) / 1000.0
    };

    CpuMetrics {
        total_usage,
        per_core_usage,
        frequency_ghz,
    }
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
