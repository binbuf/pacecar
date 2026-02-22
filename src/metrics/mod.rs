pub mod cpu;
pub mod disk;
pub mod gpu;
pub mod memory;
pub mod network;

use cpu::CpuMetrics;
use disk::DiskMetrics;
use gpu::GpuMetrics;
use memory::MemoryMetrics;
use network::NetworkMetrics;

use sysinfo::{Disks, Networks, System};

use std::sync::mpsc;
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::{Duration, Instant};

/// A point-in-time snapshot of all system metrics.
#[derive(Debug, Clone)]
pub struct MetricsSnapshot {
    pub timestamp: Instant,
    pub cpu: CpuMetrics,
    pub memory: MemoryMetrics,
    pub gpu: Option<GpuMetrics>,
    pub network: NetworkMetrics,
    pub disk: DiskMetrics,
}

/// Trait for collecting system metrics. Implementations must be `Send` so they
/// can run on a background thread.
#[cfg_attr(test, mockall::automock)]
pub trait MetricsCollector: Send {
    fn collect(&mut self) -> MetricsSnapshot;
}

/// Shared shutdown signal that can wake a sleeping collector thread immediately.
#[derive(Clone)]
pub struct ShutdownSignal {
    inner: Arc<(Mutex<bool>, Condvar)>,
}

impl ShutdownSignal {
    pub fn new() -> Self {
        Self {
            inner: Arc::new((Mutex::new(false), Condvar::new())),
        }
    }

    /// Signal shutdown and wake any thread waiting on this signal.
    pub fn trigger(&self) {
        let (lock, cvar) = &*self.inner;
        *lock.lock().unwrap() = true;
        cvar.notify_all();
    }

    /// Check whether shutdown has been requested.
    pub fn is_triggered(&self) -> bool {
        *self.inner.0.lock().unwrap()
    }

    /// Sleep for at most `duration`, returning early if shutdown is signaled.
    fn sleep_interruptible(&self, duration: Duration) {
        let (lock, cvar) = &*self.inner;
        let guard = lock.lock().unwrap();
        if !*guard {
            let _ = cvar.wait_timeout(guard, duration);
        }
    }
}

/// Handle returned when spawning the background collector thread.
/// Drop this to signal the thread to stop.
pub struct CollectorHandle {
    shutdown: ShutdownSignal,
    thread: Option<thread::JoinHandle<()>>,
}

/// Maximum time to wait for the collector thread during shutdown.
const JOIN_TIMEOUT: Duration = Duration::from_secs(2);

impl CollectorHandle {
    /// Signal the collector thread to stop and wait for it to finish.
    pub fn shutdown(self) {
        // shutdown flag is set in Drop
        drop(self);
    }

    /// Return a clone of the shutdown signal (e.g. for CTRL+C handlers).
    pub fn shutdown_signal(&self) -> ShutdownSignal {
        self.shutdown.clone()
    }
}

impl Drop for CollectorHandle {
    fn drop(&mut self) {
        self.shutdown.trigger();
        if let Some(handle) = self.thread.take() {
            // Wait with a timeout so we never hang the process on exit.
            let (done_tx, done_rx) = mpsc::sync_channel::<()>(0);
            let _ = thread::spawn(move || {
                let _ = handle.join();
                let _ = done_tx.send(());
            });
            let _ = done_rx.recv_timeout(JOIN_TIMEOUT);
        }
    }
}

/// Spawns a background thread that calls `collector.collect()` at the given
/// interval and sends snapshots over a channel.
///
/// Returns a `CollectorHandle` (for shutdown) and a `MetricsReceiver` (for the UI).
pub fn spawn_collector(
    mut collector: Box<dyn MetricsCollector>,
    interval: Duration,
) -> (CollectorHandle, MetricsReceiver) {
    let (tx, rx) = mpsc::channel();
    let shutdown = ShutdownSignal::new();
    let shutdown_flag = shutdown.clone();

    let thread = thread::spawn(move || {
        while !shutdown_flag.is_triggered() {
            let snapshot = collector.collect();
            if tx.send(snapshot).is_err() {
                // Receiver dropped — stop collecting.
                break;
            }
            shutdown_flag.sleep_interruptible(interval);
        }
    });

    let handle = CollectorHandle {
        shutdown,
        thread: Some(thread),
    };

    (handle, MetricsReceiver { rx })
}

/// Concrete collector using sysinfo, with optional GPU provider.
pub struct SystemCollector {
    system: System,
    networks: Networks,
    disks: Disks,
    gpu_provider: Option<Box<dyn gpu::GpuProvider>>,
    prev_network: Option<network::NetworkState>,
    prev_disk: Option<disk::DiskState>,
}

impl SystemCollector {
    pub fn new() -> Self {
        let mut system = System::new();
        // Warm up CPU metrics (first read is always 0%).
        system.refresh_cpu_all();

        Self {
            system,
            networks: Networks::new_with_refreshed_list(),
            disks: Disks::new_with_refreshed_list(),
            gpu_provider: gpu::init_gpu_provider(),
            prev_network: None,
            prev_disk: None,
        }
    }
}

impl MetricsCollector for SystemCollector {
    fn collect(&mut self) -> MetricsSnapshot {
        self.system.refresh_cpu_all();
        self.system.refresh_memory();
        self.networks.refresh(false);
        self.disks.refresh(false);

        let cpu_metrics = cpu::collect_cpu(&self.system);
        let memory_metrics = memory::collect_memory(&self.system);
        let gpu_metrics = gpu::collect_gpu(&self.gpu_provider);

        let (network_metrics, net_state) =
            network::collect_network(&self.networks, &self.prev_network);
        self.prev_network = Some(net_state);

        let (disk_metrics, disk_state) = disk::collect_disk(&self.disks, &self.prev_disk);
        self.prev_disk = Some(disk_state);

        MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: cpu_metrics,
            memory: memory_metrics,
            gpu: gpu_metrics,
            network: network_metrics,
            disk: disk_metrics,
        }
    }
}

/// UI-side receiver that drains the channel and returns the most recent snapshot.
pub struct MetricsReceiver {
    rx: mpsc::Receiver<MetricsSnapshot>,
}

impl MetricsReceiver {
    /// Drain all pending snapshots and return the most recent one.
    /// Returns `None` if no snapshots have been sent yet.
    /// This is non-blocking and will never stall the render loop.
    pub fn latest(&self) -> Option<MetricsSnapshot> {
        let mut latest = None;
        while let Ok(snapshot) = self.rx.try_recv() {
            latest = Some(snapshot);
        }
        latest
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_snapshot() -> MetricsSnapshot {
        MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: CpuMetrics {
                total_usage: 42.0,
                per_core_usage: vec![40.0, 44.0],
                frequency_ghz: 3.8,
            },
            memory: MemoryMetrics {
                used_bytes: 8_000_000_000,
                total_bytes: 16_000_000_000,
                usage_percent: 50.0,
            },
            gpu: Some(GpuMetrics {
                usage_percent: 28.0,
                temperature_celsius: 72.0,
                vram_used_bytes: 2_000_000_000,
                vram_total_bytes: 8_000_000_000,
            }),
            network: NetworkMetrics {
                upload_bytes_per_sec: 1_200_000,
                download_bytes_per_sec: 12_000_000,
            },
            disk: DiskMetrics {
                read_bytes_per_sec: 45_000_000,
                write_bytes_per_sec: 12_000_000,
            },
        }
    }

    #[test]
    fn snapshot_fields_accessible() {
        let snap = make_snapshot();
        assert_eq!(snap.cpu.total_usage, 42.0);
        assert_eq!(snap.cpu.per_core_usage.len(), 2);
        assert_eq!(snap.cpu.frequency_ghz, 3.8);
        assert_eq!(snap.memory.used_bytes, 8_000_000_000);
        assert_eq!(snap.memory.total_bytes, 16_000_000_000);
        assert_eq!(snap.memory.usage_percent, 50.0);
        let gpu = snap.gpu.unwrap();
        assert_eq!(gpu.usage_percent, 28.0);
        assert_eq!(gpu.temperature_celsius, 72.0);
        assert_eq!(gpu.vram_used_bytes, 2_000_000_000);
        assert_eq!(gpu.vram_total_bytes, 8_000_000_000);
        assert_eq!(snap.network.upload_bytes_per_sec, 1_200_000);
        assert_eq!(snap.network.download_bytes_per_sec, 12_000_000);
        assert_eq!(snap.disk.read_bytes_per_sec, 45_000_000);
        assert_eq!(snap.disk.write_bytes_per_sec, 12_000_000);
    }

    #[test]
    fn snapshot_gpu_none() {
        let mut snap = make_snapshot();
        snap.gpu = None;
        assert!(snap.gpu.is_none());
    }

    #[test]
    fn mock_collector_sends_through_channel() {
        let snap = make_snapshot();
        let snap_clone = snap.clone();

        let mut mock = MockMetricsCollector::new();
        mock.expect_collect()
            .times(1..)
            .returning(move || snap_clone.clone());

        let (handle, receiver) = spawn_collector(Box::new(mock), Duration::from_millis(10));

        // Give the collector time to send at least one snapshot.
        thread::sleep(Duration::from_millis(50));

        let latest = receiver.latest();
        assert!(latest.is_some());
        let latest = latest.unwrap();
        assert_eq!(latest.cpu.total_usage, snap.cpu.total_usage);

        handle.shutdown();
    }

    #[test]
    fn latest_returns_most_recent_when_multiple_buffered() {
        let (tx, rx) = mpsc::channel();
        let receiver = MetricsReceiver { rx };

        // Send three snapshots with different CPU usage values.
        for usage in [10.0, 20.0, 30.0] {
            let mut snap = make_snapshot();
            snap.cpu.total_usage = usage;
            tx.send(snap).unwrap();
        }

        let latest = receiver.latest().unwrap();
        assert_eq!(latest.cpu.total_usage, 30.0);
    }

    #[test]
    fn latest_returns_none_when_empty() {
        let (_tx, rx) = mpsc::channel::<MetricsSnapshot>();
        let receiver = MetricsReceiver { rx };
        assert!(receiver.latest().is_none());
    }

    #[test]
    fn graceful_shutdown_stops_collector() {
        let mut mock = MockMetricsCollector::new();
        mock.expect_collect()
            .times(1..)
            .returning(|| make_snapshot());

        let (handle, _receiver) = spawn_collector(Box::new(mock), Duration::from_millis(10));

        // Let it run briefly.
        thread::sleep(Duration::from_millis(50));

        // Shutdown should complete without hanging.
        handle.shutdown();
    }

    #[test]
    fn collector_stops_when_receiver_dropped() {
        let mut mock = MockMetricsCollector::new();
        mock.expect_collect()
            .times(1..)
            .returning(|| make_snapshot());

        let (handle, receiver) = spawn_collector(Box::new(mock), Duration::from_millis(10));

        thread::sleep(Duration::from_millis(30));
        drop(receiver);
        thread::sleep(Duration::from_millis(30));

        // Thread should have exited because send failed.
        // Dropping the handle should not hang.
        drop(handle);
    }
}
