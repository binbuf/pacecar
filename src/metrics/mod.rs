pub mod cpu;
pub mod disk;
pub mod discovery;
pub mod gpu;
#[cfg(target_os = "windows")]
pub mod gpu_d3dkmt;
#[cfg(feature = "hwmon")]
pub mod hwmon;
pub mod memory;
pub mod network;
pub mod ping;

use cpu::CpuMetrics;
use disk::DiskMetrics;
use gpu::GpuMetrics;
use memory::MemoryMetrics;
use network::NetworkMetrics;
use ping::PingMetrics;

use crate::config::{Config, CpuSelection, DeviceFilter, DiskTempMode, FanSpeedMode, GpuSelection, MainboardTempMode};

use sysinfo::{Components, Disks, Networks, System};

use std::sync::mpsc;
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::{Duration, Instant};

/// Subset of Config containing device selection fields.
/// Shared between the UI (writer) and collector thread (reader) via `Arc<Mutex<_>>`.
#[derive(Debug, Clone)]
pub struct CollectorConfig {
    pub gpu_selection: GpuSelection,
    pub cpu_selection: CpuSelection,
    pub network_interface: DeviceFilter,
    pub disk_device: DeviceFilter,
    pub ping_target: String,
    pub show_disk_temperature: bool,
    pub disk_temp_mode: DiskTempMode,
    pub show_fan_speed: bool,
    pub fan_speed_mode: FanSpeedMode,
    pub show_ram_temperature: bool,
    pub show_cpu_fan_speed: bool,
    pub show_gpu_fan_speed: bool,
    pub show_mainboard_temp: bool,
    pub mainboard_temp_mode: MainboardTempMode,
}

impl CollectorConfig {
    pub fn from_config(config: &Config) -> Self {
        Self {
            gpu_selection: config.gpu_selection.clone(),
            cpu_selection: config.cpu_selection.clone(),
            network_interface: config.network_interface.clone(),
            disk_device: config.disk_device.clone(),
            ping_target: config.ping_target.clone(),
            show_disk_temperature: config.show_disk_temperature,
            disk_temp_mode: config.disk_temp_mode,
            show_fan_speed: config.show_fan_speed,
            fan_speed_mode: config.fan_speed_mode,
            show_ram_temperature: config.show_ram_temperature,
            show_cpu_fan_speed: config.show_cpu_fan_speed,
            show_gpu_fan_speed: config.show_gpu_fan_speed,
            show_mainboard_temp: config.show_mainboard_temp,
            mainboard_temp_mode: config.mainboard_temp_mode,
        }
    }
}

/// A point-in-time snapshot of all system metrics.
#[derive(Debug, Clone)]
pub struct MetricsSnapshot {
    pub timestamp: Instant,
    pub cpu: CpuMetrics,
    pub memory: MemoryMetrics,
    pub gpu: Option<GpuMetrics>,
    pub network: NetworkMetrics,
    pub disk: DiskMetrics,
    pub ping: PingMetrics,
    pub fan_rpm: Option<f32>,
    pub cpu_fan_rpm: Option<f32>,
    pub gpu_fan_rpm: Option<f32>,
    pub mainboard_temp_celsius: Option<f32>,
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
    components: Option<Components>,
    #[cfg(feature = "hwmon")]
    hwmon: Option<hwmon::HwMonitor>,
    gpu_provider: Option<Box<dyn gpu::GpuProvider>>,
    prev_network: Option<network::NetworkState>,
    prev_disk: Option<disk::DiskState>,
    shared_config: Arc<Mutex<CollectorConfig>>,
    /// Track the last GPU selection to detect changes requiring re-init.
    last_gpu_selection: GpuSelection,
}

impl SystemCollector {
    pub fn new(shared_config: Arc<Mutex<CollectorConfig>>) -> Self {
        let mut system = System::new();
        // Warm up CPU metrics (first read is always 0%).
        system.refresh_cpu_all();

        let gpu_selection = shared_config.lock().unwrap().gpu_selection.clone();
        let gpu_provider = gpu::init_gpu_provider(&gpu_selection);

        #[cfg(feature = "hwmon")]
        let hwmon = hwmon::HwMonitor::try_load();

        Self {
            system,
            networks: Networks::new_with_refreshed_list(),
            disks: Disks::new_with_refreshed_list(),
            components: None,
            #[cfg(feature = "hwmon")]
            hwmon,
            gpu_provider,
            prev_network: None,
            prev_disk: None,
            last_gpu_selection: gpu_selection,
            shared_config,
        }
    }
}

/// Read CPU temperature from sysinfo `Components`.
/// Looks for components whose label contains "cpu" (case-insensitive).
/// Returns the highest temperature found, or `None` if no CPU sensor exists.
fn read_cpu_temperature(components: &Components) -> Option<f32> {
    let mut max_temp: Option<f32> = None;
    for component in components.iter() {
        let label = component.label().to_lowercase();
        if label.contains("cpu") || label.contains("core") || label.contains("tctl") {
            if let Some(temp) = component.temperature() {
                max_temp = Some(max_temp.map_or(temp, |m: f32| m.max(temp)));
            }
        }
    }
    max_temp
}

impl MetricsCollector for SystemCollector {
    fn collect(&mut self) -> MetricsSnapshot {
        self.system.refresh_cpu_all();
        self.system.refresh_memory();
        self.networks.refresh(false);
        self.disks.refresh(false);

        // Lazily initialize Components on the background thread to avoid
        // COM threading conflicts (sysinfo initializes COM as MTA, which
        // conflicts with winit's OleInitialize requiring STA on the main thread).
        let components = self.components.get_or_insert_with(Components::new_with_refreshed_list);
        components.refresh(false);

        // Read the current device config snapshot.
        let cfg = self.shared_config.lock().unwrap().clone();

        // Re-init GPU provider if selection changed.
        if cfg.gpu_selection != self.last_gpu_selection {
            self.gpu_provider = gpu::init_gpu_provider(&cfg.gpu_selection);
            self.last_gpu_selection = cfg.gpu_selection.clone();
        }

        #[cfg(feature = "hwmon")]
        let cpu_temp = self
            .hwmon
            .as_ref()
            .and_then(|h| h.cpu_temp(&cfg.cpu_selection))
            .or_else(|| read_cpu_temperature(components));

        #[cfg(not(feature = "hwmon"))]
        let cpu_temp = read_cpu_temperature(components);
        let cpu_metrics = cpu::collect_cpu_selected(&self.system, &cfg.cpu_selection, cpu_temp);
        #[allow(unused_mut)]
        let mut memory_metrics = memory::collect_memory(&self.system);
        let gpu_metrics = gpu::collect_gpu(&self.gpu_provider);

        let (network_metrics, net_state) =
            network::collect_network(&self.networks, &self.prev_network, &cfg.network_interface);
        self.prev_network = Some(net_state);

        let (mut disk_metrics, disk_state) =
            disk::collect_disk(&self.disks, &self.prev_disk, &cfg.disk_device);
        self.prev_disk = Some(disk_state);

        // Disk temperature via hwmon (LibreHardwareMonitor).
        #[cfg(feature = "hwmon")]
        if cfg.show_disk_temperature {
            if let Some(ref hwmon) = self.hwmon {
                let disk_temps = hwmon.disk_temps();
                if !disk_temps.is_empty() {
                    disk_metrics.temperature_celsius = match cfg.disk_temp_mode {
                        DiskTempMode::SelectedDisk => {
                            match &cfg.disk_device {
                                DeviceFilter::Named(mount) => {
                                    // Match LHWM name against sysinfo disk name (case-insensitive substring).
                                    let mount_lower = mount.to_lowercase();
                                    disk_temps
                                        .iter()
                                        .find(|(name, _)| {
                                            let name_lower = name.to_lowercase();
                                            name_lower.contains(&mount_lower) || mount_lower.contains(&name_lower)
                                        })
                                        .map(|(_, t)| *t)
                                        .or_else(|| disk_temps.iter().map(|(_, t)| *t).reduce(f32::max))
                                }
                                DeviceFilter::All => {
                                    // Fall back to highest when All.
                                    disk_temps.iter().map(|(_, t)| *t).reduce(f32::max)
                                }
                            }
                        }
                        DiskTempMode::Highest => {
                            disk_temps.iter().map(|(_, t)| *t).reduce(f32::max)
                        }
                        DiskTempMode::Average => {
                            let sum: f32 = disk_temps.iter().map(|(_, t)| *t).sum();
                            Some(sum / disk_temps.len() as f32)
                        }
                    };
                }
            }
        }

        // Fan speed via hwmon.
        #[allow(unused_mut)]
        let mut fan_rpm: Option<f32> = None;
        #[cfg(feature = "hwmon")]
        if cfg.show_fan_speed {
            if let Some(ref hwmon) = self.hwmon {
                let fans = hwmon.fan_speeds();
                if !fans.is_empty() {
                    fan_rpm = match cfg.fan_speed_mode {
                        FanSpeedMode::Highest => fans.iter().map(|(_, r)| *r).reduce(f32::max),
                        FanSpeedMode::Average => {
                            let sum: f32 = fans.iter().map(|(_, r)| *r).sum();
                            Some(sum / fans.len() as f32)
                        }
                    };
                }
            }
        }

        // RAM temperature via hwmon (DIMM sensors from motherboard SMBus).
        #[cfg(feature = "hwmon")]
        if cfg.show_ram_temperature {
            if let Some(ref hwmon) = self.hwmon {
                memory_metrics.temperature_celsius = hwmon.ram_temp();
            }
        }

        // CPU fan speed via hwmon.
        #[allow(unused_mut)]
        let mut cpu_fan_rpm: Option<f32> = None;
        #[cfg(feature = "hwmon")]
        if cfg.show_cpu_fan_speed {
            if let Some(ref hwmon) = self.hwmon {
                cpu_fan_rpm = hwmon.cpu_fan_speed();
            }
        }

        // GPU fan speed: prefer hwmon RPM, fall back to NVML percentage via GpuMetrics.
        #[allow(unused_mut)]
        let mut gpu_fan_rpm: Option<f32> = None;
        #[cfg(feature = "hwmon")]
        if cfg.show_gpu_fan_speed {
            if let Some(ref hwmon) = self.hwmon {
                gpu_fan_rpm = hwmon.gpu_fan_speed();
            }
        }
        if gpu_fan_rpm.is_none() && cfg.show_gpu_fan_speed {
            if let Some(ref gpu) = gpu_metrics {
                gpu_fan_rpm = gpu.fan_speed_percent;
            }
        }

        // Mainboard temperature via hwmon.
        #[allow(unused_mut)]
        let mut mainboard_temp_celsius: Option<f32> = None;
        #[cfg(feature = "hwmon")]
        if cfg.show_mainboard_temp {
            if let Some(ref hwmon) = self.hwmon {
                let mb_temps = hwmon.mainboard_temps();
                if !mb_temps.is_empty() {
                    mainboard_temp_celsius = match cfg.mainboard_temp_mode {
                        MainboardTempMode::Highest => mb_temps.iter().map(|(_, t)| *t).reduce(f32::max),
                        MainboardTempMode::Average => {
                            let sum: f32 = mb_temps.iter().map(|(_, t)| *t).sum();
                            Some(sum / mb_temps.len() as f32)
                        }
                    };
                }
            }
        }

        let ping_metrics = ping::collect_ping(&cfg.ping_target);

        MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: cpu_metrics,
            memory: memory_metrics,
            gpu: gpu_metrics,
            network: network_metrics,
            disk: disk_metrics,
            ping: ping_metrics,
            fan_rpm,
            cpu_fan_rpm,
            gpu_fan_rpm,
            mainboard_temp_celsius,
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
                frequency_ghz: 3.8,
                temperature_celsius: None,
            },
            memory: MemoryMetrics {
                used_bytes: 8_000_000_000,
                total_bytes: 16_000_000_000,
                usage_percent: 50.0,
                temperature_celsius: None,
            },
            gpu: Some(GpuMetrics {
                usage_percent: 28.0,
                temperature_celsius: 72.0,
                vram_used_bytes: 2_000_000_000,
                vram_total_bytes: 8_000_000_000,
                fan_speed_percent: None,
            }),
            network: NetworkMetrics {
                upload_bytes_per_sec: 1_200_000,
                download_bytes_per_sec: 12_000_000,
            },
            disk: DiskMetrics {
                read_bytes_per_sec: 45_000_000,
                write_bytes_per_sec: 12_000_000,
                temperature_celsius: None,
            },
            ping: PingMetrics {
                latency_ms: Some(12.0),
            },
            fan_rpm: None,
            cpu_fan_rpm: None,
            gpu_fan_rpm: None,
            mainboard_temp_celsius: None,
        }
    }

    #[test]
    fn snapshot_fields_accessible() {
        let snap = make_snapshot();
        assert_eq!(snap.cpu.total_usage, 42.0);
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
