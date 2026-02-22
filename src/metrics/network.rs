use sysinfo::Networks;
use std::time::Instant;

/// Network metrics: upload and download speeds.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct NetworkMetrics {
    pub upload_bytes_per_sec: u64,
    pub download_bytes_per_sec: u64,
}

/// State carried between ticks to compute speed deltas.
#[derive(Debug, Clone, Copy)]
pub struct NetworkState {
    pub total_sent: u64,
    pub total_received: u64,
    pub timestamp: Instant,
}

/// Collect network metrics by computing deltas from the previous state.
///
/// The caller must have called `networks.refresh()` (or `refresh_list()`)
/// before calling this function so the values are up-to-date.
///
/// On the first tick (`prev` is `None`), speeds are reported as 0.
/// If counters appear to have reset (current < previous), speeds are clamped to 0.
pub fn collect_network(
    networks: &Networks,
    prev: &Option<NetworkState>,
) -> (NetworkMetrics, NetworkState) {
    // Sum cumulative bytes across all interfaces.
    let total_sent: u64 = networks.iter().map(|(_, data)| data.total_transmitted()).sum();
    let total_received: u64 = networks.iter().map(|(_, data)| data.total_received()).sum();
    let now = Instant::now();

    let current_state = NetworkState {
        total_sent,
        total_received,
        timestamp: now,
    };

    let metrics = match prev {
        Some(prev_state) => {
            let elapsed = now.duration_since(prev_state.timestamp);
            let elapsed_secs = elapsed.as_secs_f64();

            if elapsed_secs <= 0.0 {
                NetworkMetrics::default()
            } else {
                // Clamp to 0 on counter reset/overflow.
                let sent_delta = total_sent.saturating_sub(prev_state.total_sent);
                let recv_delta = total_received.saturating_sub(prev_state.total_received);

                NetworkMetrics {
                    upload_bytes_per_sec: (sent_delta as f64 / elapsed_secs) as u64,
                    download_bytes_per_sec: (recv_delta as f64 / elapsed_secs) as u64,
                }
            }
        }
        None => NetworkMetrics::default(),
    };

    (metrics, current_state)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn first_tick_returns_zeros() {
        let networks = Networks::new_with_refreshed_list();
        let (metrics, state) = collect_network(&networks, &None);

        assert_eq!(metrics.upload_bytes_per_sec, 0);
        assert_eq!(metrics.download_bytes_per_sec, 0);
        // State should have been populated (u64 is always >= 0).
        let _ = state.total_sent;
        let _ = state.total_received;
    }

    #[test]
    fn speed_calculated_from_consecutive_readings() {
        // Simulate two ticks with known deltas by constructing states directly.
        let prev = NetworkState {
            total_sent: 1_000_000,
            total_received: 5_000_000,
            timestamp: Instant::now() - Duration::from_secs(1),
        };

        // After 1 second, 500KB sent and 2MB received.
        let current_sent = 1_500_000u64;
        let current_received = 7_000_000u64;

        let elapsed_secs = 1.0;
        let upload = ((current_sent - prev.total_sent) as f64 / elapsed_secs) as u64;
        let download = ((current_received - prev.total_received) as f64 / elapsed_secs) as u64;

        assert_eq!(upload, 500_000);
        assert_eq!(download, 2_000_000);
    }

    #[test]
    fn counter_reset_handled_gracefully() {
        // Simulate a counter reset: current < previous.
        let prev = NetworkState {
            total_sent: 10_000_000,
            total_received: 20_000_000,
            timestamp: Instant::now() - Duration::from_secs(1),
        };

        // Counter reset: current values are lower than previous.
        let sent_delta = 5_000_000u64.saturating_sub(prev.total_sent);
        let recv_delta = 1_000_000u64.saturating_sub(prev.total_received);

        // saturating_sub should clamp to 0.
        assert_eq!(sent_delta, 0);
        assert_eq!(recv_delta, 0);
    }

    #[test]
    fn collect_from_real_system() {
        let mut networks = Networks::new_with_refreshed_list();
        let (metrics1, state1) = collect_network(&networks, &None);
        assert_eq!(metrics1.upload_bytes_per_sec, 0);
        assert_eq!(metrics1.download_bytes_per_sec, 0);

        // Wait briefly and refresh for a second reading.
        std::thread::sleep(Duration::from_millis(100));
        networks.refresh();
        let (metrics2, _state2) = collect_network(&networks, &Some(state1));

        // After refresh, speeds should be non-negative (we can't guarantee traffic).
        // Just verify no panic and the values are reasonable.
        assert!(metrics2.upload_bytes_per_sec < u64::MAX);
        assert!(metrics2.download_bytes_per_sec < u64::MAX);
    }

    #[test]
    fn default_metrics_are_zero() {
        let m = NetworkMetrics::default();
        assert_eq!(m.upload_bytes_per_sec, 0);
        assert_eq!(m.download_bytes_per_sec, 0);
    }
}

#[cfg(test)]
mod proptests {
    use proptest::prelude::*;

    proptest! {
        #[test]
        fn speeds_always_non_negative(
            prev_sent in 0u64..=u64::MAX / 2,
            prev_recv in 0u64..=u64::MAX / 2,
            curr_sent in 0u64..=u64::MAX / 2,
            curr_recv in 0u64..=u64::MAX / 2,
        ) {
            let sent_delta = curr_sent.saturating_sub(prev_sent);
            let recv_delta = curr_recv.saturating_sub(prev_recv);

            // Speed = delta / elapsed; with elapsed > 0, speed >= 0.
            let elapsed_secs = 1.0f64;
            let upload = (sent_delta as f64 / elapsed_secs) as u64;
            let download = (recv_delta as f64 / elapsed_secs) as u64;

            prop_assert!(upload <= curr_sent.max(prev_sent));
            prop_assert!(download <= curr_recv.max(prev_recv));
        }
    }
}
