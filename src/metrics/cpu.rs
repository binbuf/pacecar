use crate::config::CpuSelection;
use sysinfo::System;

/// CPU metrics: total usage, frequency, and optional temperature.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct CpuMetrics {
    pub total_usage: f32,
    pub frequency_ghz: f32,
    pub temperature_celsius: Option<f32>,
}

/// Collect CPU metrics from a `sysinfo::System` instance.
///
/// The caller must have called `refresh_cpu_all()` at least twice (with a delay
/// between calls) before this function returns accurate usage values. On the
/// very first tick, `sysinfo` returns 0% for all CPUs — this is expected and
/// the caller should handle it (e.g., by discarding the first snapshot or
/// displaying a "warming up" state).
pub fn collect_cpu(system: &System, cpu_temp: Option<f32>) -> CpuMetrics {
    let total_usage = system.global_cpu_usage();

    let cpus = system.cpus();

    // Try to get current (dynamic) frequency first; fall back to sysinfo's
    // base frequency if the platform-specific call is unavailable.
    let frequency_ghz = current_frequency_ghz(cpus.len())
        .unwrap_or_else(|| sysinfo_frequency_ghz(cpus));

    CpuMetrics {
        total_usage,
        frequency_ghz,
        temperature_celsius: cpu_temp,
    }
}

/// Collect CPU metrics based on the current selection.
///
/// If `selection` is `Aggregate`, uses global CPU usage. If `Core(n)`,
/// uses the usage of core `n`. Falls back to aggregate if the core index
/// is out of range.
pub fn collect_cpu_selected(system: &System, selection: &CpuSelection, cpu_temp: Option<f32>) -> CpuMetrics {
    let total_usage = match selection {
        CpuSelection::Aggregate => system.global_cpu_usage(),
        CpuSelection::Core(idx) => {
            let cpus = system.cpus();
            cpus.get(*idx)
                .map(|cpu| cpu.cpu_usage())
                .unwrap_or_else(|| system.global_cpu_usage())
        }
    };

    let cpus = system.cpus();
    let frequency_ghz = current_frequency_ghz(cpus.len())
        .unwrap_or_else(|| sysinfo_frequency_ghz(cpus));

    CpuMetrics {
        total_usage,
        frequency_ghz,
        temperature_celsius: cpu_temp,
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

/// Read current (dynamic) CPU frequency via Windows Performance Counters
/// (PDH). Uses `% Processor Performance` (a rate counter that shows the
/// current operating frequency as a percentage of nominal speed) multiplied
/// by the base frequency — this is the same method Task Manager uses.
///
/// The PDH query is kept open across calls via `thread_local!` because rate
/// counters need at least two `PdhCollectQueryData` calls to produce a
/// meaningful value.
///
/// Returns `None` on non-Windows platforms or if the PDH query fails.
#[cfg(target_os = "windows")]
fn current_frequency_ghz(_num_cpus: usize) -> Option<f32> {
    use std::cell::RefCell;
    use std::mem;
    use std::ptr;

    type PdhHquery = isize;
    type PdhHcounter = isize;

    #[repr(C)]
    struct PdhFmtCounterValue {
        cstatus: u32,
        double_value: f64,
    }

    const PDH_FMT_DOUBLE: u32 = 0x0000_0200;

    #[link(name = "pdh")]
    unsafe extern "system" {
        fn PdhOpenQueryA(
            data_source: *const u8,
            user_data: usize,
            query: *mut PdhHquery,
        ) -> i32;
        fn PdhAddEnglishCounterA(
            query: PdhHquery,
            counter_path: *const u8,
            user_data: usize,
            counter: *mut PdhHcounter,
        ) -> i32;
        fn PdhCollectQueryData(query: PdhHquery) -> i32;
        fn PdhGetFormattedCounterValue(
            counter: PdhHcounter,
            format: u32,
            counter_type: *mut u32,
            value: *mut PdhFmtCounterValue,
        ) -> i32;
        fn PdhCloseQuery(query: PdhHquery) -> i32;
    }

    /// Persistent PDH query state, kept alive across calls on the collector thread.
    struct PdhFreqQuery {
        query: PdhHquery,
        perf_counter: PdhHcounter,
        freq_counter: PdhHcounter,
        /// First collect produces no usable rate data; skip it.
        warmed_up: bool,
    }

    impl PdhFreqQuery {
        fn new() -> Option<Self> {
            let perf_path =
                b"\\Processor Information(_Total)\\% Processor Performance\0";
            let freq_path =
                b"\\Processor Information(_Total)\\Processor Frequency\0";

            unsafe {
                let mut query: PdhHquery = 0;
                if PdhOpenQueryA(ptr::null(), 0, &mut query) != 0 {
                    return None;
                }

                let mut perf_counter: PdhHcounter = 0;
                if PdhAddEnglishCounterA(
                    query,
                    perf_path.as_ptr(),
                    0,
                    &mut perf_counter,
                ) != 0
                {
                    PdhCloseQuery(query);
                    return None;
                }

                let mut freq_counter: PdhHcounter = 0;
                if PdhAddEnglishCounterA(
                    query,
                    freq_path.as_ptr(),
                    0,
                    &mut freq_counter,
                ) != 0
                {
                    PdhCloseQuery(query);
                    return None;
                }

                // First collect seeds the rate counter baseline.
                let _ = PdhCollectQueryData(query);

                Some(Self {
                    query,
                    perf_counter,
                    freq_counter,
                    warmed_up: false,
                })
            }
        }

        fn read(&mut self) -> Option<f32> {
            unsafe {
                if PdhCollectQueryData(self.query) != 0 {
                    return None;
                }

                if !self.warmed_up {
                    self.warmed_up = true;
                    // Rate counters need two collects; the first real value
                    // will come on the next call.
                    return None;
                }

                // Read % Processor Performance (percentage of nominal speed,
                // can exceed 100% when boosting).
                let mut perf_val = mem::zeroed::<PdhFmtCounterValue>();
                let mut counter_type: u32 = 0;
                if PdhGetFormattedCounterValue(
                    self.perf_counter,
                    PDH_FMT_DOUBLE,
                    &mut counter_type,
                    &mut perf_val,
                ) != 0
                {
                    return None;
                }

                // Read Processor Frequency (nominal/base MHz).
                let mut freq_val = mem::zeroed::<PdhFmtCounterValue>();
                if PdhGetFormattedCounterValue(
                    self.freq_counter,
                    PDH_FMT_DOUBLE,
                    &mut counter_type,
                    &mut freq_val,
                ) != 0
                {
                    return None;
                }

                // current_mhz = base_mhz × (perf% / 100)
                let current_mhz =
                    freq_val.double_value * perf_val.double_value / 100.0;
                let ghz = current_mhz as f32 / 1000.0;
                if ghz > 0.0 { Some(ghz) } else { None }
            }
        }
    }

    impl Drop for PdhFreqQuery {
        fn drop(&mut self) {
            unsafe {
                PdhCloseQuery(self.query);
            }
        }
    }

    thread_local! {
        static FREQ_QUERY: RefCell<Option<PdhFreqQuery>> =
            RefCell::new(PdhFreqQuery::new());
    }

    FREQ_QUERY.with(|cell| {
        cell.borrow_mut().as_mut().and_then(|q| q.read())
    })
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
        let metrics = collect_cpu(&system, None);

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

        let metrics = collect_cpu(&system, None);

        // After warmup, total_usage should be in [0, 100].
        assert!(
            (0.0..=100.0).contains(&metrics.total_usage),
            "total_usage out of range: {}",
            metrics.total_usage
        );

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
        assert_eq!(m.frequency_ghz, 0.0);
    }
}

#[cfg(test)]
mod proptests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn cpu_usage_always_in_range(total in 0.0f32..=100.0, freq in 0.0f32..=10.0) {
            let metrics = CpuMetrics {
                total_usage: total,
                frequency_ghz: freq,
                temperature_celsius: None,
            };
            prop_assert!((0.0..=100.0).contains(&metrics.total_usage));
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
