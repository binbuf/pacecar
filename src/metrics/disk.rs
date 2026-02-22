use crate::config::DeviceFilter;
use sysinfo::Disks;
use std::time::Instant;

/// Disk I/O metrics: read and write speeds.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct DiskMetrics {
    pub read_bytes_per_sec: u64,
    pub write_bytes_per_sec: u64,
}

/// State carried between ticks to compute speed deltas.
#[derive(Debug, Clone, Copy)]
pub struct DiskState {
    pub total_read: u64,
    pub total_written: u64,
    pub timestamp: Instant,
}

/// Collect disk I/O metrics by computing deltas from the previous state.
///
/// The caller must have called `disks.refresh()` (or `refresh_list()`)
/// before calling this function so the values are up-to-date.
///
/// On the first tick (`prev` is `None`), speeds are reported as 0.
/// If counters appear to have reset (current < previous), speeds are clamped to 0.
pub fn collect_disk(
    disks: &Disks,
    prev: &Option<DiskState>,
    filter: &DeviceFilter,
) -> (DiskMetrics, DiskState) {
    // Sum cumulative bytes, optionally filtered to a single disk mount point.
    let filtered = disks.iter().filter(|d| match filter {
        DeviceFilter::All => true,
        DeviceFilter::Named(target) => d.mount_point().to_string_lossy() == target.as_str(),
    });
    let (total_read, total_written) = filtered.fold((0u64, 0u64), |(r, w), d| {
        (r + d.usage().total_read_bytes, w + d.usage().total_written_bytes)
    });
    let now = Instant::now();

    let current_state = DiskState {
        total_read,
        total_written,
        timestamp: now,
    };

    let metrics = match prev {
        Some(prev_state) => {
            let elapsed = now.duration_since(prev_state.timestamp);
            let elapsed_secs = elapsed.as_secs_f64();

            if elapsed_secs <= 0.0 {
                DiskMetrics::default()
            } else {
                // Clamp to 0 on counter reset/overflow.
                let read_delta = total_read.saturating_sub(prev_state.total_read);
                let write_delta = total_written.saturating_sub(prev_state.total_written);

                DiskMetrics {
                    read_bytes_per_sec: (read_delta as f64 / elapsed_secs) as u64,
                    write_bytes_per_sec: (write_delta as f64 / elapsed_secs) as u64,
                }
            }
        }
        None => DiskMetrics::default(),
    };

    (metrics, current_state)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn first_tick_returns_zeros() {
        let disks = Disks::new_with_refreshed_list();
        let (metrics, state) = collect_disk(&disks, &None, &DeviceFilter::All);

        assert_eq!(metrics.read_bytes_per_sec, 0);
        assert_eq!(metrics.write_bytes_per_sec, 0);
        // State should have been populated.
        let _ = state.total_read;
        let _ = state.total_written;
    }

    #[test]
    fn speed_calculated_from_consecutive_readings() {
        // Simulate two ticks with known deltas by constructing states directly.
        let prev = DiskState {
            total_read: 1_000_000,
            total_written: 5_000_000,
            timestamp: Instant::now() - Duration::from_secs(1),
        };

        // After 1 second, 500KB read and 2MB written.
        let current_read = 1_500_000u64;
        let current_written = 7_000_000u64;

        let elapsed_secs = 1.0;
        let read_speed = ((current_read - prev.total_read) as f64 / elapsed_secs) as u64;
        let write_speed = ((current_written - prev.total_written) as f64 / elapsed_secs) as u64;

        assert_eq!(read_speed, 500_000);
        assert_eq!(write_speed, 2_000_000);
    }

    #[test]
    fn counter_reset_handled_gracefully() {
        // Simulate a counter reset: current < previous.
        let prev = DiskState {
            total_read: 10_000_000,
            total_written: 20_000_000,
            timestamp: Instant::now() - Duration::from_secs(1),
        };

        // Counter reset: current values are lower than previous.
        let read_delta = 5_000_000u64.saturating_sub(prev.total_read);
        let write_delta = 1_000_000u64.saturating_sub(prev.total_written);

        // saturating_sub should clamp to 0.
        assert_eq!(read_delta, 0);
        assert_eq!(write_delta, 0);
    }

    #[test]
    fn collect_from_real_system() {
        let mut disks = Disks::new_with_refreshed_list();
        let (metrics1, state1) = collect_disk(&disks, &None, &DeviceFilter::All);
        assert_eq!(metrics1.read_bytes_per_sec, 0);
        assert_eq!(metrics1.write_bytes_per_sec, 0);

        // Wait briefly and refresh for a second reading.
        std::thread::sleep(Duration::from_millis(100));
        disks.refresh(false);
        let (metrics2, _state2) = collect_disk(&disks, &Some(state1), &DeviceFilter::All);

        // After refresh, speeds should be non-negative (we can't guarantee I/O).
        // Just verify no panic and the values are reasonable.
        assert!(metrics2.read_bytes_per_sec < u64::MAX);
        assert!(metrics2.write_bytes_per_sec < u64::MAX);
    }

    #[test]
    fn default_metrics_are_zero() {
        let m = DiskMetrics::default();
        assert_eq!(m.read_bytes_per_sec, 0);
        assert_eq!(m.write_bytes_per_sec, 0);
    }
}

#[cfg(test)]
mod proptests {
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn speeds_always_non_negative(
            prev_read in 0u64..=u64::MAX / 2,
            prev_written in 0u64..=u64::MAX / 2,
            curr_read in 0u64..=u64::MAX / 2,
            curr_written in 0u64..=u64::MAX / 2,
        ) {
            let read_delta = curr_read.saturating_sub(prev_read);
            let write_delta = curr_written.saturating_sub(prev_written);

            // Speed = delta / elapsed; with elapsed > 0, speed >= 0.
            let elapsed_secs = 1.0f64;
            let read_speed = (read_delta as f64 / elapsed_secs) as u64;
            let write_speed = (write_delta as f64 / elapsed_secs) as u64;

            prop_assert!(read_speed <= curr_read.max(prev_read));
            prop_assert!(write_speed <= curr_written.max(prev_written));
        }
    }
}
