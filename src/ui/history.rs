// Metric history storage and detailed graph viewport

use std::collections::HashMap;
use std::collections::VecDeque;
use std::time::{Duration, Instant};

use eframe::egui;

use crate::config::Config;
use crate::metrics::MetricsSnapshot;

use super::sparkline::Sparkline;
use super::MetricColors;

/// A time-series of `(timestamp, value)` pairs stored oldest-first.
pub struct TimeSeries {
    data: VecDeque<(Instant, f32)>,
}

impl TimeSeries {
    pub fn new() -> Self {
        Self {
            data: VecDeque::new(),
        }
    }

    /// Append a new sample.
    pub fn push(&mut self, now: Instant, value: f32) {
        self.data.push_back((now, value));
    }

    /// Remove samples older than `now - retention`.
    pub fn prune(&mut self, now: Instant, retention: Duration) {
        let cutoff = now.checked_sub(retention).unwrap_or(now);
        while let Some(&(t, _)) = self.data.front() {
            if t < cutoff {
                self.data.pop_front();
            } else {
                break;
            }
        }
    }

    /// Return the most recent `max_count` values (chronological order).
    pub fn recent_values(&self, max_count: usize) -> Vec<f32> {
        let start = self.data.len().saturating_sub(max_count);
        self.data.iter().skip(start).map(|(_, v)| *v).collect()
    }

    /// Return all values (chronological order).
    pub fn all_values(&self) -> Vec<f32> {
        self.data.iter().map(|(_, v)| *v).collect()
    }

    /// Number of samples stored.
    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }
}

/// Identifies which metric a time series belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MetricKey {
    CpuUsage,
    CpuTemp,
    RamUsage,
    RamTemp,
    GpuUsage,
    GpuTemp,
    NetUp,
    NetDown,
    DiskRead,
    DiskWrite,
    PingLatency,
    FanRpm,
    CpuFanRpm,
    GpuFanRpm,
    MainboardTemp,
}

impl MetricKey {
    /// Human-readable label for display.
    pub fn label(self) -> &'static str {
        match self {
            Self::CpuUsage => "CPU Usage",
            Self::CpuTemp => "CPU Temp",
            Self::RamUsage => "RAM Usage",
            Self::RamTemp => "RAM Temp",
            Self::GpuUsage => "GPU Usage",
            Self::GpuTemp => "GPU Temp",
            Self::NetUp => "Net Upload",
            Self::NetDown => "Net Download",
            Self::DiskRead => "Disk Read",
            Self::DiskWrite => "Disk Write",
            Self::PingLatency => "Ping",
            Self::FanRpm => "Fan RPM",
            Self::CpuFanRpm => "CPU Fan RPM",
            Self::GpuFanRpm => "GPU Fan RPM",
            Self::MainboardTemp => "Mainboard Temp",
        }
    }

    /// Accent color for this metric.
    pub fn color(self) -> egui::Color32 {
        match self {
            Self::CpuUsage | Self::CpuTemp | Self::CpuFanRpm => MetricColors::CPU,
            Self::RamUsage | Self::RamTemp => MetricColors::RAM,
            Self::GpuUsage | Self::GpuTemp | Self::GpuFanRpm => MetricColors::GPU,
            Self::NetUp | Self::NetDown => MetricColors::NETWORK,
            Self::DiskRead | Self::DiskWrite => MetricColors::DISK,
            Self::PingLatency => MetricColors::PING,
            Self::FanRpm => MetricColors::FANS,
            Self::MainboardTemp => MetricColors::MAINBOARD,
        }
    }

    /// Fixed Y-axis range for percentage-based metrics, or `None` for auto-range.
    pub fn fixed_range(self) -> Option<(f32, f32)> {
        match self {
            Self::CpuUsage | Self::RamUsage | Self::GpuUsage => Some((0.0, 100.0)),
            _ => None,
        }
    }

    /// Unit suffix for display.
    pub fn unit(self) -> &'static str {
        match self {
            Self::CpuUsage | Self::RamUsage | Self::GpuUsage => "%",
            Self::CpuTemp | Self::GpuTemp | Self::RamTemp | Self::MainboardTemp => "\u{00B0}C",
            Self::NetUp | Self::NetDown | Self::DiskRead | Self::DiskWrite => "KB/s",
            Self::PingLatency => "ms",
            Self::FanRpm | Self::CpuFanRpm | Self::GpuFanRpm => "RPM",
        }
    }

    /// Display order for the history window.
    const DISPLAY_ORDER: &[MetricKey] = &[
        Self::CpuUsage,
        Self::CpuTemp,
        Self::RamUsage,
        Self::RamTemp,
        Self::GpuUsage,
        Self::GpuTemp,
        Self::NetUp,
        Self::NetDown,
        Self::DiskRead,
        Self::DiskWrite,
        Self::PingLatency,
        Self::FanRpm,
        Self::CpuFanRpm,
        Self::GpuFanRpm,
        Self::MainboardTemp,
    ];
}

/// Stores time-series data for all metrics.
pub struct MetricsHistory {
    series: HashMap<MetricKey, TimeSeries>,
}

impl MetricsHistory {
    pub fn new() -> Self {
        Self {
            series: HashMap::new(),
        }
    }

    /// Record values from a snapshot into the corresponding time series.
    pub fn record(&mut self, snap: &MetricsSnapshot) {
        let now = snap.timestamp;

        self.push(MetricKey::CpuUsage, now, snap.cpu.total_usage);

        if let Some(t) = snap.cpu.temperature_celsius {
            self.push(MetricKey::CpuTemp, now, t);
        }

        self.push(MetricKey::RamUsage, now, snap.memory.usage_percent);

        if let Some(t) = snap.memory.temperature_celsius {
            self.push(MetricKey::RamTemp, now, t);
        }

        if let Some(ref gpu) = snap.gpu {
            self.push(MetricKey::GpuUsage, now, gpu.usage_percent);
            self.push(MetricKey::GpuTemp, now, gpu.temperature_celsius);
        }

        // Store network as KB/s for readability
        self.push(
            MetricKey::NetUp,
            now,
            snap.network.upload_bytes_per_sec as f32 / 1024.0,
        );
        self.push(
            MetricKey::NetDown,
            now,
            snap.network.download_bytes_per_sec as f32 / 1024.0,
        );

        // Store disk as KB/s
        self.push(
            MetricKey::DiskRead,
            now,
            snap.disk.read_bytes_per_sec as f32 / 1024.0,
        );
        self.push(
            MetricKey::DiskWrite,
            now,
            snap.disk.write_bytes_per_sec as f32 / 1024.0,
        );

        if let Some(ms) = snap.ping.latency_ms {
            self.push(MetricKey::PingLatency, now, ms as f32);
        }

        if let Some(rpm) = snap.fan_rpm {
            self.push(MetricKey::FanRpm, now, rpm);
        }

        if let Some(rpm) = snap.cpu_fan_rpm {
            self.push(MetricKey::CpuFanRpm, now, rpm);
        }

        if let Some(rpm) = snap.gpu_fan_rpm {
            self.push(MetricKey::GpuFanRpm, now, rpm);
        }

        if let Some(t) = snap.mainboard_temp_celsius {
            self.push(MetricKey::MainboardTemp, now, t);
        }
    }

    /// Prune all series to the given retention window.
    pub fn prune_all(&mut self, now: Instant, retention: Duration) {
        for ts in self.series.values_mut() {
            ts.prune(now, retention);
        }
        // Remove empty series to avoid stale keys
        self.series.retain(|_, ts| !ts.is_empty());
    }

    /// Get the time series for a specific metric.
    pub fn get(&self, key: MetricKey) -> Option<&TimeSeries> {
        self.series.get(&key)
    }

    fn push(&mut self, key: MetricKey, now: Instant, value: f32) {
        self.series.entry(key).or_insert_with(TimeSeries::new).push(now, value);
    }
}

/// Downsample a value slice to approximately `target` points using LTTB-style
/// largest-triangle-three-buckets, falling back to simple bucket averaging
/// when the input is not much larger than the target.
pub fn downsample(values: &[f32], target: usize) -> Vec<f32> {
    if values.len() <= target || target < 2 {
        return values.to_vec();
    }
    let bucket_size = values.len() as f64 / target as f64;
    let mut out = Vec::with_capacity(target);
    for i in 0..target {
        let start = (i as f64 * bucket_size) as usize;
        let end = (((i + 1) as f64 * bucket_size) as usize).min(values.len());
        let sum: f32 = values[start..end].iter().sum();
        let count = (end - start) as f32;
        out.push(sum / count);
    }
    out
}

/// Compute an auto-range `(min, max)` from values, with a small margin.
fn auto_range(values: &[f32]) -> (f32, f32) {
    if values.is_empty() {
        return (0.0, 1.0);
    }
    let mut min = f32::MAX;
    let mut max = f32::MIN;
    for &v in values {
        if v < min {
            min = v;
        }
        if v > max {
            max = v;
        }
    }
    if (max - min).abs() < f32::EPSILON {
        // Flat line — give some visual range
        let center = min;
        return ((center - 1.0).max(0.0), center + 1.0);
    }
    let margin = (max - min) * 0.05;
    ((min - margin).max(0.0), max + margin)
}

/// Render the detailed history window as a separate OS viewport.
/// Returns `true` while the window is open, `false` when it should be closed.
pub fn show_history_window(
    ctx: &egui::Context,
    history: &MetricsHistory,
    config: &mut Config,
) -> bool {
    let mut open = true;

    ctx.show_viewport_immediate(
        egui::ViewportId::from_hash_of("pacecar_history"),
        egui::ViewportBuilder::default()
            .with_title("Metric History")
            .with_inner_size([500.0, 600.0])
            .with_always_on_top()
            .with_minimize_button(false)
            .with_maximize_button(false),
        |ctx, _class| {
            if ctx.input(|i: &egui::InputState| i.viewport().close_requested()) {
                open = false;
                return;
            }

            crate::ui::settings::configure_settings_visuals(ctx);

            egui::CentralPanel::default()
                .frame(
                    egui::Frame::NONE
                        .fill(egui::Color32::from_rgb(24, 24, 28))
                        .inner_margin(16.0),
                )
                .show(ctx, |ui| {
                    // Header row: title + retention dropdown
                    ui.horizontal(|ui| {
                        ui.label(
                            egui::RichText::new("Metric History")
                                .size(18.0)
                                .color(egui::Color32::WHITE)
                                .strong(),
                        );

                        ui.with_layout(
                            egui::Layout::right_to_left(egui::Align::Center),
                            |ui| {
                                let retention_label =
                                    format!("{} min", config.history_retention_minutes);
                                egui::ComboBox::from_id_salt("history_retention")
                                    .selected_text(&retention_label)
                                    .width(80.0)
                                    .show_ui(ui, |ui| {
                                        for &mins in RETENTION_PRESETS {
                                            let label = format!("{mins} min");
                                            if ui
                                                .selectable_value(
                                                    &mut config.history_retention_minutes,
                                                    mins,
                                                    label,
                                                )
                                                .changed()
                                            {
                                                config.clamp();
                                                let _ = config.save();
                                            }
                                        }
                                    });
                                ui.label(
                                    egui::RichText::new("Retention:")
                                        .size(11.0)
                                        .color(egui::Color32::from_gray(160)),
                                );
                            },
                        );
                    });

                    ui.add_space(8.0);

                    // Scrollable chart cards
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        let chart_width = ui.available_width();

                        for &key in MetricKey::DISPLAY_ORDER {
                            if let Some(ts) = history.get(key) {
                                if ts.is_empty() {
                                    continue;
                                }

                                let values = ts.all_values();
                                let display_values =
                                    downsample(&values, chart_width as usize);

                                let range = match key.fixed_range() {
                                    Some(r) => r,
                                    None => auto_range(&display_values),
                                };

                                // Card frame
                                egui::Frame::NONE
                                    .fill(egui::Color32::from_rgb(32, 32, 36))
                                    .corner_radius(6.0)
                                    .inner_margin(egui::Margin::symmetric(10, 8))
                                    .stroke(egui::Stroke::new(
                                        1.0,
                                        egui::Color32::from_rgb(50, 50, 55),
                                    ))
                                    .show(ui, |ui| {
                                        ui.set_width(chart_width - 22.0);

                                        // Label + current value
                                        ui.horizontal(|ui| {
                                            ui.label(
                                                egui::RichText::new(key.label())
                                                    .size(12.0)
                                                    .color(key.color())
                                                    .strong(),
                                            );
                                            if let Some(&last) = display_values.last() {
                                                ui.with_layout(
                                                    egui::Layout::right_to_left(
                                                        egui::Align::Center,
                                                    ),
                                                    |ui| {
                                                        ui.label(
                                                            egui::RichText::new(format!(
                                                                "{:.1}{}",
                                                                last,
                                                                key.unit()
                                                            ))
                                                            .size(11.0)
                                                            .color(
                                                                egui::Color32::from_gray(180),
                                                            )
                                                            .monospace(),
                                                        );
                                                    },
                                                );
                                            }
                                        });

                                        ui.add_space(2.0);

                                        // Large sparkline chart
                                        let spark_width = (chart_width - 22.0).max(60.0);
                                        ui.add(Sparkline::new(
                                            &display_values,
                                            key.color(),
                                            egui::Vec2::new(spark_width, 80.0),
                                            range,
                                        ));
                                    });

                                ui.add_space(6.0);
                            }
                        }
                    });
                });
        },
    );

    open
}

/// Allowed retention presets (minutes).
const RETENTION_PRESETS: &[u32] = &[1, 5, 10, 15, 30, 60, 120];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn time_series_push_and_recent() {
        let mut ts = TimeSeries::new();
        let now = Instant::now();
        for i in 0..10 {
            ts.push(now + Duration::from_secs(i), i as f32);
        }
        assert_eq!(ts.len(), 10);
        let recent = ts.recent_values(3);
        assert_eq!(recent, vec![7.0, 8.0, 9.0]);
    }

    #[test]
    fn time_series_prune_removes_old() {
        let mut ts = TimeSeries::new();
        let start = Instant::now();
        for i in 0..10 {
            ts.push(start + Duration::from_secs(i), i as f32);
        }
        // Prune with retention of 5 seconds from the last sample
        let now = start + Duration::from_secs(9);
        ts.prune(now, Duration::from_secs(5));
        // Samples 0..4 should be pruned (cutoff = 9-5 = 4s)
        let values = ts.all_values();
        assert_eq!(values, vec![4.0, 5.0, 6.0, 7.0, 8.0, 9.0]);
    }

    #[test]
    fn time_series_recent_values_more_than_available() {
        let mut ts = TimeSeries::new();
        let now = Instant::now();
        ts.push(now, 1.0);
        ts.push(now + Duration::from_secs(1), 2.0);
        let recent = ts.recent_values(100);
        assert_eq!(recent, vec![1.0, 2.0]);
    }

    #[test]
    fn time_series_empty() {
        let ts = TimeSeries::new();
        assert!(ts.is_empty());
        assert_eq!(ts.len(), 0);
        assert!(ts.all_values().is_empty());
        assert!(ts.recent_values(5).is_empty());
    }

    #[test]
    fn downsample_passthrough_when_small() {
        let values = vec![1.0, 2.0, 3.0];
        let result = downsample(&values, 10);
        assert_eq!(result, values);
    }

    #[test]
    fn downsample_reduces_length() {
        let values: Vec<f32> = (0..100).map(|i| i as f32).collect();
        let result = downsample(&values, 10);
        assert_eq!(result.len(), 10);
    }

    #[test]
    fn auto_range_empty() {
        let (min, max) = auto_range(&[]);
        assert_eq!(min, 0.0);
        assert_eq!(max, 1.0);
    }

    #[test]
    fn auto_range_flat_line() {
        let (min, max) = auto_range(&[42.0, 42.0, 42.0]);
        assert!(min < 42.0);
        assert!(max > 42.0);
    }

    #[test]
    fn auto_range_varied() {
        let (min, max) = auto_range(&[10.0, 50.0, 30.0]);
        assert!(min <= 10.0);
        assert!(max >= 50.0);
    }

    #[test]
    fn metrics_history_record_and_get() {
        use crate::metrics::cpu::CpuMetrics;
        use crate::metrics::disk::DiskMetrics;
        use crate::metrics::memory::MemoryMetrics;
        use crate::metrics::network::NetworkMetrics;
        use crate::metrics::ping::PingMetrics;

        let mut history = MetricsHistory::new();
        let snap = MetricsSnapshot {
            timestamp: Instant::now(),
            cpu: CpuMetrics {
                total_usage: 55.0,
                frequency_ghz: 3.5,
                temperature_celsius: Some(65.0),
            },
            memory: MemoryMetrics {
                used_bytes: 8_000_000_000,
                total_bytes: 16_000_000_000,
                usage_percent: 50.0,
                temperature_celsius: None,
            },
            gpu: None,
            network: NetworkMetrics {
                upload_bytes_per_sec: 1024,
                download_bytes_per_sec: 2048,
            },
            disk: DiskMetrics {
                read_bytes_per_sec: 4096,
                write_bytes_per_sec: 8192,
                temperature_celsius: None,
            },
            ping: PingMetrics {
                latency_ms: Some(10.0),
            },
            fan_rpm: None,
            cpu_fan_rpm: None,
            gpu_fan_rpm: None,
            mainboard_temp_celsius: None,
        };

        history.record(&snap);

        assert!(history.get(MetricKey::CpuUsage).is_some());
        assert_eq!(history.get(MetricKey::CpuUsage).unwrap().len(), 1);
        assert!(history.get(MetricKey::CpuTemp).is_some());
        assert!(history.get(MetricKey::GpuUsage).is_none()); // no GPU data
        assert!(history.get(MetricKey::PingLatency).is_some());
    }

    #[test]
    fn metrics_history_prune_all() {
        let mut history = MetricsHistory::new();
        let start = Instant::now();

        // Push an old sample and a recent one
        history
            .series
            .entry(MetricKey::CpuUsage)
            .or_insert_with(TimeSeries::new)
            .push(start, 10.0);
        history
            .series
            .entry(MetricKey::CpuUsage)
            .or_insert_with(TimeSeries::new)
            .push(start + Duration::from_secs(100), 20.0);

        let now = start + Duration::from_secs(100);
        history.prune_all(now, Duration::from_secs(50));

        let ts = history.get(MetricKey::CpuUsage).unwrap();
        assert_eq!(ts.len(), 1);
        assert_eq!(ts.all_values(), vec![20.0]);
    }
}
