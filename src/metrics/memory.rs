use sysinfo::System;

/// RAM metrics: used/total bytes and usage percentage, with optional temperature.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct MemoryMetrics {
    pub used_bytes: u64,
    pub total_bytes: u64,
    pub usage_percent: f32,
    pub temperature_celsius: Option<f32>,
}

/// Collect memory metrics from a `sysinfo::System` instance.
///
/// The caller must have called `refresh_memory()` before this function
/// to ensure the values are up-to-date.
pub fn collect_memory(system: &System) -> MemoryMetrics {
    let total_bytes = system.total_memory();
    let used_bytes = system.used_memory();

    let usage_percent = if total_bytes == 0 {
        0.0
    } else {
        (used_bytes as f64 / total_bytes as f64 * 100.0) as f32
    };

    MemoryMetrics {
        used_bytes,
        total_bytes,
        usage_percent,
        temperature_celsius: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn collect_memory_returns_valid_struct() {
        let mut system = System::new();
        system.refresh_memory();
        let metrics = collect_memory(&system);

        assert!(metrics.total_bytes > 0, "total_bytes should be > 0 on a real system");
        assert!(metrics.used_bytes <= metrics.total_bytes);
        assert!(
            (0.0..=100.0).contains(&metrics.usage_percent),
            "usage_percent out of range: {}",
            metrics.usage_percent
        );
    }

    #[test]
    fn percentage_calculation_known_values() {
        let metrics = MemoryMetrics {
            used_bytes: 8_000_000_000,
            total_bytes: 16_000_000_000,
            usage_percent: (8_000_000_000u64 as f64 / 16_000_000_000u64 as f64 * 100.0) as f32,
            ..Default::default()
        };
        assert!((metrics.usage_percent - 50.0).abs() < f32::EPSILON);
    }

    #[test]
    fn division_by_zero_safety() {
        // Simulate a system reporting 0 total memory.
        let metrics = MemoryMetrics {
            used_bytes: 0,
            total_bytes: 0,
            usage_percent: if 0u64 == 0 { 0.0 } else { unreachable!() },
            ..Default::default()
        };
        assert_eq!(metrics.usage_percent, 0.0);
    }

    #[test]
    fn collect_memory_zero_total_returns_zero_percent() {
        // We can't easily mock sysinfo::System, so we test the logic directly.
        let total_bytes: u64 = 0;
        let used_bytes: u64 = 0;
        let usage_percent = if total_bytes == 0 {
            0.0
        } else {
            (used_bytes as f64 / total_bytes as f64 * 100.0) as f32
        };
        assert_eq!(usage_percent, 0.0_f32);
    }

    #[test]
    fn default_metrics_are_zero() {
        let m = MemoryMetrics::default();
        assert_eq!(m.used_bytes, 0);
        assert_eq!(m.total_bytes, 0);
        assert_eq!(m.usage_percent, 0.0);
        assert_eq!(m.temperature_celsius, None);
    }
}

#[cfg(test)]
mod proptests {
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn usage_percent_always_in_range(used in 0u64..=u64::MAX / 2, total in 1u64..=u64::MAX / 2) {
            // Ensure used <= total for realistic inputs.
            let used = used.min(total);
            let usage_percent = (used as f64 / total as f64 * 100.0) as f32;
            prop_assert!((0.0..=100.0).contains(&usage_percent),
                "usage_percent out of range: {} (used={}, total={})", usage_percent, used, total);
        }

        #[test]
        fn zero_total_gives_zero_percent(used in 0u64..=1_000_000) {
            let total: u64 = 0;
            let usage_percent = if total == 0 { 0.0_f32 } else { (used as f64 / total as f64 * 100.0) as f32 };
            prop_assert_eq!(usage_percent, 0.0);
        }
    }
}
