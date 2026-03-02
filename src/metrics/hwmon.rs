//! Runtime FFI wrapper around the hwmon-shim NativeAOT DLL.
//!
//! Loads `hwmon-shim.dll` from the executable's directory at runtime via `libloading`.
//! Falls back gracefully if the DLL is missing or initialization fails.

use crate::config::CpuSelection;
use libloading::{Library, Symbol};
use std::path::PathBuf;

/// Check whether the PawnIO kernel driver is installed and, if not, offer to
/// install it via a native Windows MessageBox + PowerShell script.
///
/// This should be called **before** `HwMonitor::try_load()` so the driver is
/// available when LibreHardwareMonitor initializes.  The check is a quick
/// `sc query PawnIO` — effectively free when the driver is already present.
pub fn ensure_pawnio_driver() {
    if is_pawnio_installed() {
        return;
    }

    // Driver is missing — ask the user whether to install it.
    let response = show_install_prompt();
    if response == MessageBoxResult::Ok {
        run_installer_script();
    }
}

/// Returns `true` if the PawnIO driver service is registered on the system.
fn is_pawnio_installed() -> bool {
    std::process::Command::new("sc")
        .args(["query", "PawnIO"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

#[derive(PartialEq)]
enum MessageBoxResult {
    Ok,
    Cancel,
}

/// Show a native Windows MessageBox explaining the PawnIO requirement.
fn show_install_prompt() -> MessageBoxResult {
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    fn wide(s: &str) -> Vec<u16> {
        OsStr::new(s).encode_wide().chain(std::iter::once(0)).collect()
    }

    // MB_OKCANCEL | MB_ICONINFORMATION
    const MB_OKCANCEL: u32 = 0x0000_0001;
    const MB_ICONINFORMATION: u32 = 0x0000_0040;
    const IDOK: i32 = 1;

    unsafe extern "system" {
        fn MessageBoxW(hwnd: *mut core::ffi::c_void, text: *const u16, caption: *const u16, flags: u32) -> i32;
    }

    let text = wide(
        "The PawnIO kernel driver is required for CPU temperature monitoring.\n\n\
         PawnIO is a signed driver used by LibreHardwareMonitor 0.9.6+.\n\n\
         Click OK to download and install it (requires Administrator),\n\
         or Cancel to skip (temperature readings will be unavailable)."
    );
    let caption = wide("Pacecar — Driver Required");

    let result = unsafe {
        MessageBoxW(
            std::ptr::null_mut(),
            text.as_ptr(),
            caption.as_ptr(),
            MB_OKCANCEL | MB_ICONINFORMATION,
        )
    };

    if result == IDOK { MessageBoxResult::Ok } else { MessageBoxResult::Cancel }
}

/// Launch the `install-pawnio.ps1` script next to the executable and wait for
/// it to finish.
fn run_installer_script() {
    let script = match std::env::current_exe().ok().and_then(|e| {
        e.parent().map(|p| p.join("install-pawnio.ps1"))
    }) {
        Some(p) if p.exists() => p,
        _ => {
            eprintln!("warn: install-pawnio.ps1 not found next to executable");
            return;
        }
    };

    match std::process::Command::new("powershell")
        .args([
            "-ExecutionPolicy", "Bypass",
            "-File",
        ])
        .arg(&script)
        .status()
    {
        Ok(s) if s.success() => {
            eprintln!("[pacecar] PawnIO installer completed successfully");
        }
        Ok(s) => {
            eprintln!("[pacecar] PawnIO installer exited with {s}");
        }
        Err(e) => {
            eprintln!("[pacecar] Failed to launch PowerShell: {e}");
        }
    }
}

const MAX_CORES: usize = 128;
const MAX_DISKS: usize = 32;
const MAX_FANS: usize = 16;
const MAX_MB_TEMPS: usize = 32;
const DISK_NAME_LEN: usize = 128;
const FAN_NAME_LEN: usize = 128;
const MB_TEMP_NAME_LEN: usize = 128;

/// Matches the C# `CpuTemps` struct layout.
#[repr(C)]
struct CpuTemps {
    package_temp: f32,
    core_count: i32,
    core_temps: [f32; MAX_CORES],
}

/// Matches the C# `DiskTemps` struct layout.
#[repr(C)]
struct DiskTemps {
    disk_count: i32,
    temps: [f32; MAX_DISKS],
    names: [u8; MAX_DISKS * DISK_NAME_LEN],
}

/// Matches the C# `FanSpeeds` struct layout.
#[repr(C)]
struct FanSpeeds {
    fan_count: i32,
    rpms: [f32; MAX_FANS],
    names: [u8; MAX_FANS * FAN_NAME_LEN],
}

/// Matches the C# `MainboardTemps` struct layout.
#[repr(C)]
struct MainboardTemps {
    temp_count: i32,
    temps: [f32; MAX_MB_TEMPS],
    names: [u8; MAX_MB_TEMPS * MB_TEMP_NAME_LEN],
}

/// Matches the C# `RamTemp` struct layout.
#[repr(C)]
struct RamTemp {
    temperature: f32,
}

/// Matches the C# `GpuFanSpeed` struct layout.
#[repr(C)]
struct GpuFanSpeed {
    rpm: f32,
}

type HwmonInitFn = unsafe extern "C" fn() -> i32;
type HwmonCpuTempsFn = unsafe extern "C" fn(*mut CpuTemps) -> i32;
type HwmonDiskTempsFn = unsafe extern "C" fn(*mut DiskTemps) -> i32;
type HwmonFanSpeedsFn = unsafe extern "C" fn(*mut FanSpeeds) -> i32;
type HwmonMainboardTempsFn = unsafe extern "C" fn(*mut MainboardTemps) -> i32;
type HwmonRamTempFn = unsafe extern "C" fn(*mut RamTemp) -> i32;
type HwmonGpuFanSpeedFn = unsafe extern "C" fn(*mut GpuFanSpeed) -> i32;
type HwmonShutdownFn = unsafe extern "C" fn();

pub struct HwMonitor {
    _library: Library,
    cpu_temps_fn: HwmonCpuTempsFn,
    disk_temps_fn: HwmonDiskTempsFn,
    fan_speeds_fn: HwmonFanSpeedsFn,
    mainboard_temps_fn: HwmonMainboardTempsFn,
    ram_temp_fn: HwmonRamTempFn,
    gpu_fan_speed_fn: Option<HwmonGpuFanSpeedFn>,
    shutdown_fn: HwmonShutdownFn,
}

// NativeAOT DLLs are thread-safe for these stateless-style calls.
unsafe impl Send for HwMonitor {}

impl HwMonitor {
    /// Try to load the hwmon-shim DLL from the executable's directory.
    /// Returns `None` if the DLL is missing, can't be loaded, or init fails.
    pub fn try_load() -> Option<Self> {
        let log = Self::log_path();
        let mut log_msg = |msg: &str| {
            use std::io::Write;
            if let Some(ref p) = log {
                if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(p) {
                    let _ = writeln!(f, "{msg}");
                }
            }
        };

        let dll_path = match Self::dll_path() {
            Some(p) => { log_msg(&format!("DLL found: {}", p.display())); p }
            None => { log_msg("DLL not found"); return None; }
        };

        let library = match unsafe { Library::new(&dll_path) } {
            Ok(l) => { log_msg("Library loaded"); l }
            Err(e) => { log_msg(&format!("Library load failed: {e}")); return None; }
        };

        let (init_fn, cpu_temps_fn, disk_temps_fn, fan_speeds_fn, mainboard_temps_fn, ram_temp_fn, shutdown_fn) = unsafe {
            let init: Symbol<HwmonInitFn> = match library.get(b"hwmon_init") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_init symbol not found: {e}")); return None; }
            };
            let temps: Symbol<HwmonCpuTempsFn> = match library.get(b"hwmon_cpu_temps") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_cpu_temps symbol not found: {e}")); return None; }
            };
            let disk_temps: Symbol<HwmonDiskTempsFn> = match library.get(b"hwmon_disk_temps") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_disk_temps symbol not found: {e}")); return None; }
            };
            let fan_speeds: Symbol<HwmonFanSpeedsFn> = match library.get(b"hwmon_fan_speeds") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_fan_speeds symbol not found: {e}")); return None; }
            };
            let mainboard_temps: Symbol<HwmonMainboardTempsFn> = match library.get(b"hwmon_mainboard_temps") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_mainboard_temps symbol not found: {e}")); return None; }
            };
            let ram_temp: Symbol<HwmonRamTempFn> = match library.get(b"hwmon_ram_temp") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_ram_temp symbol not found: {e}")); return None; }
            };
            let shutdown: Symbol<HwmonShutdownFn> = match library.get(b"hwmon_shutdown") {
                Ok(s) => s,
                Err(e) => { log_msg(&format!("hwmon_shutdown symbol not found: {e}")); return None; }
            };
            log_msg("All symbols resolved");
            (*init, *temps, *disk_temps, *fan_speeds, *mainboard_temps, *ram_temp, *shutdown)
        };

        // Optional symbols — missing ones don't block initialization.
        let gpu_fan_speed_fn = unsafe {
            match library.get::<HwmonGpuFanSpeedFn>(b"hwmon_gpu_fan_speed") {
                Ok(s) => { log_msg("hwmon_gpu_fan_speed symbol found"); Some(*s) }
                Err(_) => { log_msg("hwmon_gpu_fan_speed symbol not found (optional, skipping)"); None }
            }
        };

        let result = unsafe { init_fn() };
        if result != 0 {
            log_msg(&format!("hwmon_init returned {result}"));
            return None;
        }
        log_msg("hwmon_init succeeded");

        Some(Self {
            _library: library,
            cpu_temps_fn,
            disk_temps_fn,
            fan_speeds_fn,
            mainboard_temps_fn,
            ram_temp_fn,
            gpu_fan_speed_fn,
            shutdown_fn,
        })
    }

    /// Query CPU temperature based on the current selection.
    ///
    /// - `Aggregate`: returns the package/Tctl temp, or the average of all core temps.
    /// - `Core(n)`: returns core `n`'s temp, falling back to package temp.
    pub fn cpu_temp(&self, selection: &CpuSelection) -> Option<f32> {
        let mut temps = CpuTemps {
            package_temp: -1.0,
            core_count: 0,
            core_temps: [0.0; MAX_CORES],
        };

        let rc = unsafe { (self.cpu_temps_fn)(&mut temps) };

        // Diagnostic logging
        use std::io::Write;
        if let Some(p) = Self::log_path() {
            if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(p) {
                let _ = writeln!(f, "cpu_temps rc={rc} pkg={} cores={} first_4={:?}",
                    temps.package_temp, temps.core_count,
                    &temps.core_temps[..4.min(temps.core_count.max(0) as usize)]);
            }
        }

        if rc != 0 {
            return None;
        }

        match selection {
            CpuSelection::Core(idx) => {
                let idx = *idx;
                // Try the specific core temp first, fall back to package.
                if idx < temps.core_count as usize && temps.core_temps[idx] > 0.0 {
                    Some(temps.core_temps[idx])
                } else if temps.package_temp > 0.0 {
                    Some(temps.package_temp)
                } else {
                    self.average_core_temp(&temps)
                }
            }
            CpuSelection::Aggregate => {
                // Prefer package temp, fall back to average of cores.
                if temps.package_temp > 0.0 {
                    Some(temps.package_temp)
                } else {
                    self.average_core_temp(&temps)
                }
            }
        }
    }

    fn average_core_temp(&self, temps: &CpuTemps) -> Option<f32> {
        let count = temps.core_count as usize;
        if count == 0 {
            return None;
        }
        let sum: f32 = temps.core_temps[..count].iter().filter(|t| **t > 0.0).sum();
        let valid = temps.core_temps[..count].iter().filter(|t| **t > 0.0).count();
        if valid > 0 { Some(sum / valid as f32) } else { None }
    }

    /// Query disk temperatures from LibreHardwareMonitor.
    ///
    /// Returns a list of `(name, temperature)` pairs for each storage device
    /// that exposes a temperature sensor.
    pub fn disk_temps(&self) -> Vec<(String, f32)> {
        let mut temps = DiskTemps {
            disk_count: 0,
            temps: [0.0; MAX_DISKS],
            names: [0u8; MAX_DISKS * DISK_NAME_LEN],
        };

        let rc = unsafe { (self.disk_temps_fn)(&mut temps) };
        if rc != 0 {
            return Vec::new();
        }

        let count = (temps.disk_count as usize).min(MAX_DISKS);
        let mut result = Vec::with_capacity(count);
        for i in 0..count {
            let name_start = i * DISK_NAME_LEN;
            let name_slice = &temps.names[name_start..name_start + DISK_NAME_LEN];
            let nul_pos = name_slice.iter().position(|&b| b == 0).unwrap_or(DISK_NAME_LEN);
            let name = String::from_utf8_lossy(&name_slice[..nul_pos]).to_string();
            result.push((name, temps.temps[i]));
        }
        result
    }

    /// Query fan speeds from motherboard SuperIO.
    ///
    /// Returns a list of `(name, rpm)` pairs.
    pub fn fan_speeds(&self) -> Vec<(String, f32)> {
        let mut data = FanSpeeds {
            fan_count: 0,
            rpms: [0.0; MAX_FANS],
            names: [0u8; MAX_FANS * FAN_NAME_LEN],
        };

        let rc = unsafe { (self.fan_speeds_fn)(&mut data) };
        if rc != 0 {
            return Vec::new();
        }

        let count = (data.fan_count as usize).min(MAX_FANS);
        let mut result = Vec::with_capacity(count);
        for i in 0..count {
            let name_start = i * FAN_NAME_LEN;
            let name_slice = &data.names[name_start..name_start + FAN_NAME_LEN];
            let nul_pos = name_slice.iter().position(|&b| b == 0).unwrap_or(FAN_NAME_LEN);
            let name = String::from_utf8_lossy(&name_slice[..nul_pos]).to_string();
            result.push((name, data.rpms[i]));
        }
        result
    }

    /// Query mainboard temperatures from SuperIO.
    ///
    /// Returns a list of `(name, temperature)` pairs.
    pub fn mainboard_temps(&self) -> Vec<(String, f32)> {
        let mut data = MainboardTemps {
            temp_count: 0,
            temps: [0.0; MAX_MB_TEMPS],
            names: [0u8; MAX_MB_TEMPS * MB_TEMP_NAME_LEN],
        };

        let rc = unsafe { (self.mainboard_temps_fn)(&mut data) };
        if rc != 0 {
            return Vec::new();
        }

        let count = (data.temp_count as usize).min(MAX_MB_TEMPS);
        let mut result = Vec::with_capacity(count);
        for i in 0..count {
            let name_start = i * MB_TEMP_NAME_LEN;
            let name_slice = &data.names[name_start..name_start + MB_TEMP_NAME_LEN];
            let nul_pos = name_slice.iter().position(|&b| b == 0).unwrap_or(MB_TEMP_NAME_LEN);
            let name = String::from_utf8_lossy(&name_slice[..nul_pos]).to_string();
            result.push((name, data.temps[i]));
        }
        result
    }

    /// Query RAM temperature from DIMM sensors.
    ///
    /// Returns `Some(temp)` if a DIMM temperature sensor is available, `None` otherwise.
    pub fn ram_temp(&self) -> Option<f32> {
        let mut data = RamTemp {
            temperature: -1.0,
        };

        let rc = unsafe { (self.ram_temp_fn)(&mut data) };
        if rc != 0 {
            return None;
        }

        if data.temperature > 0.0 { Some(data.temperature) } else { None }
    }

    /// Query the CPU fan speed from motherboard fan sensors.
    ///
    /// Looks for a fan whose name contains "cpu" (case-insensitive).
    /// Returns the RPM value if found.
    pub fn cpu_fan_speed(&self) -> Option<f32> {
        let fans = self.fan_speeds();
        // Look for a fan named "CPU Fan", "CPU", etc.
        fans.iter()
            .find(|(name, _)| name.to_lowercase().contains("cpu"))
            .map(|(_, rpm)| *rpm)
            .or_else(|| {
                // Fallback: first fan (often "Fan #1" which is typically the CPU fan)
                fans.first().map(|(_, rpm)| *rpm)
            })
    }

    /// Query GPU fan speed from GPU hardware sensors via LibreHardwareMonitor.
    ///
    /// Returns the highest GPU fan RPM if available.
    pub fn gpu_fan_speed(&self) -> Option<f32> {
        let func = self.gpu_fan_speed_fn?;
        let mut data = GpuFanSpeed { rpm: -1.0 };
        let rc = unsafe { func(&mut data) };
        if rc != 0 {
            return None;
        }
        if data.rpm > 0.0 { Some(data.rpm) } else { None }
    }

    /// Find the DLL next to the running executable.
    fn dll_path() -> Option<PathBuf> {
        let exe = std::env::current_exe().ok()?;
        let path = exe.parent()?.join("hwmon-shim.dll");
        if path.exists() { Some(path) } else { None }
    }

    fn log_path() -> Option<PathBuf> {
        std::env::current_exe().ok().and_then(|e| e.parent().map(|p| p.join("pacecar-hwmon.log")))
    }
}

impl Drop for HwMonitor {
    fn drop(&mut self) {
        unsafe { (self.shutdown_fn)() };
    }
}
