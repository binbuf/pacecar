//! One-time device discovery at startup.
//!
//! Enumerates available GPUs, CPU cores, network interfaces, and disk devices
//! so the settings UI can present them as selectable options.

use sysinfo::{Disks, Networks, System};

/// Information about a discovered GPU adapter.
#[derive(Debug, Clone)]
pub struct GpuDeviceInfo {
    /// Adapter index (NVML device index or D3DKMT adapter ordinal).
    pub index: u32,
    /// Human-readable adapter name.
    pub name: String,
    /// Which backend discovered this GPU.
    pub provider: GpuProviderKind,
}

/// Which GPU backend provides metrics for this adapter.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum GpuProviderKind {
    Nvml,
    D3dkmt,
}

/// Information about a discovered disk device.
#[derive(Debug, Clone)]
pub struct DiskDeviceInfo {
    /// Device name (e.g. `sda`, `PhysicalDrive0`).
    pub name: String,
    /// Mount point (e.g. `C:\`, `/`).
    pub mount_point: String,
    /// Label shown in the settings UI.
    pub display_label: String,
}

/// All devices discovered at startup.
#[derive(Debug, Clone)]
pub struct AvailableDevices {
    pub gpus: Vec<GpuDeviceInfo>,
    pub cpu_core_count: usize,
    pub network_interfaces: Vec<String>,
    pub disks: Vec<DiskDeviceInfo>,
}

/// Discover all available devices on the system.
///
/// This is called once at startup. GPU discovery uses NVML (when the `nvidia`
/// feature is enabled) and D3DKMT (on Windows). CPU/network/disk use `sysinfo`.
pub fn discover_devices() -> AvailableDevices {
    let mut gpus = Vec::new();

    // --- NVML GPUs ---
    #[cfg(feature = "nvidia")]
    {
        if let Ok(nvml) = nvml_wrapper::Nvml::init() {
            if let Ok(count) = nvml.device_count() {
                for i in 0..count {
                    if let Ok(device) = nvml.device_by_index(i) {
                        let name = device.name().unwrap_or_else(|_| format!("NVIDIA GPU {i}"));
                        gpus.push(GpuDeviceInfo {
                            index: i,
                            name,
                            provider: GpuProviderKind::Nvml,
                        });
                    }
                }
            }
        }
    }

    // --- D3DKMT GPUs (Windows only) ---
    #[cfg(target_os = "windows")]
    {
        let d3dkmt_gpus = super::gpu_d3dkmt::enumerate_adapters();
        for info in d3dkmt_gpus {
            // Skip adapters already found via NVML (match by name substring).
            let dominated = gpus.iter().any(|g: &GpuDeviceInfo| {
                g.provider == GpuProviderKind::Nvml
                    && (g.name.contains(&info.name) || info.name.contains(&g.name))
            });
            if !dominated {
                gpus.push(GpuDeviceInfo {
                    index: info.index,
                    name: info.name,
                    provider: GpuProviderKind::D3dkmt,
                });
            }
        }
    }

    // --- CPU ---
    let mut system = System::new();
    system.refresh_cpu_all();
    let cpu_core_count = system.cpus().len();

    // --- Network ---
    let networks = Networks::new_with_refreshed_list();
    let network_interfaces: Vec<String> = networks.iter().map(|(name, _)| name.to_string()).collect();

    // --- Disks ---
    let sysinfo_disks = Disks::new_with_refreshed_list();
    let disks: Vec<DiskDeviceInfo> = sysinfo_disks
        .iter()
        .map(|d| {
            let name = d.name().to_string_lossy().to_string();
            let mount_point = d.mount_point().to_string_lossy().to_string();
            let display_label = if name.is_empty() {
                mount_point.clone()
            } else {
                format!("{name} ({mount_point})")
            };
            DiskDeviceInfo {
                name,
                mount_point,
                display_label,
            }
        })
        .collect();

    AvailableDevices {
        gpus,
        cpu_core_count,
        network_interfaces,
        disks,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn discover_returns_valid_structure() {
        let devices = discover_devices();
        // CPU core count should be at least 1 on any real system.
        assert!(devices.cpu_core_count >= 1);
        // Network and disks may be empty in CI but should not panic.
        let _ = &devices.network_interfaces;
        let _ = &devices.disks;
        let _ = &devices.gpus;
    }
}
