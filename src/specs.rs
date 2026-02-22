// Static hardware specs, collected once at startup via PowerShell.

use std::sync::mpsc;
use std::thread;

pub struct SystemSpecs {
    pub cpu_name: String,
    pub mainboard: String,
    pub memory_summary: String,
    pub graphics: String,
    pub display: String,
}

/// Spawn a background thread that collects hardware specs and sends the result
/// over a channel. Returns the receiver immediately.
pub fn spawn_specs_collector() -> mpsc::Receiver<SystemSpecs> {
    let (tx, rx) = mpsc::channel();
    thread::spawn(move || {
        let specs = collect_specs();
        let _ = tx.send(specs);
    });
    rx
}

fn collect_specs() -> SystemSpecs {
    // CPU name from sysinfo (no shell needed)
    let cpu_name = {
        use sysinfo::System;
        let mut sys = System::new();
        sys.refresh_cpu_all();
        sys.cpus()
            .first()
            .map(|c| c.brand().to_string())
            .unwrap_or_else(|| "Unknown CPU".into())
    };

    // Run a single PowerShell invocation for all remaining queries
    let ps_script = r#"
$board = Get-CimInstance Win32_BaseBoard
$boardStr = "$($board.Manufacturer) $($board.Product)"

$mem = Get-CimInstance Win32_PhysicalMemory
$stickCount = ($mem | Measure-Object).Count
$totalBytes = ($mem | Measure-Object -Property Capacity -Sum).Sum
$totalGB = [math]::Round($totalBytes / 1GB)
$speed = ($mem | Select-Object -First 1).Speed
$memType = ($mem | Select-Object -First 1).SMBIOSMemoryType
$ddrLabel = switch ($memType) { 26 { "DDR4" } 34 { "DDR5" } default { "DDR" } }
if ($stickCount -gt 1) {
    $stickGB = [math]::Round(($mem | Select-Object -First 1).Capacity / 1GB)
    $memStr = "$totalGB GB $ddrLabel-$speed ($stickCount x $stickGB GB)"
} else {
    $memStr = "$totalGB GB $ddrLabel-$speed"
}

$gpu = Get-CimInstance Win32_VideoController | Where-Object { $_.CurrentHorizontalResolution -gt 0 } | Select-Object -First 1
if (-not $gpu) { $gpu = Get-CimInstance Win32_VideoController | Select-Object -First 1 }
$gpuName = $gpu.Name

# AdapterRAM is uint32 (caps at 4 GB). Read the 64-bit qwMemorySize from the registry instead.
$vramBytes = [uint64]0
$regPath = 'HKLM:\SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}'
Get-ChildItem $regPath -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -match '^\d+$' } | ForEach-Object {
    $props = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
    if ($props.'DriverDesc' -eq $gpuName -and $props.'HardwareInformation.qwMemorySize') {
        $vramBytes = [uint64]$props.'HardwareInformation.qwMemorySize'
    }
}
$vramGB = [math]::Round($vramBytes / 1GB)
if ($vramGB -gt 0) {
    $gpuStr = "$gpuName ($vramGB GB)"
} else {
    $gpuStr = "$gpuName"
}

# Primary monitor resolution + refresh rate via EnumDisplaySettingsW.
# Use a raw byte[] buffer to avoid struct layout/marshaling issues across .NET versions.
# Passing null as device name targets the OS primary display.
# DEVMODEW offsets: dmSize=68, dmPelsWidth=172, dmPelsHeight=176, dmDisplayFrequency=184.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class PrimaryDisplay {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplaySettingsW(string name, int mode, byte[] dm);
    public static string Query() {
        byte[] dm = new byte[512];
        BitConverter.GetBytes((ushort)220).CopyTo(dm, 68);
        if (!EnumDisplaySettingsW(null, -1, dm)) return "Unknown";
        uint w  = BitConverter.ToUInt32(dm, 172);
        uint h  = BitConverter.ToUInt32(dm, 176);
        uint hz = BitConverter.ToUInt32(dm, 184);
        return w + " x " + h + " @ " + hz + " Hz";
    }
}
'@
$dispStr = [PrimaryDisplay]::Query()

Write-Output "BOARD:$boardStr"
Write-Output "MEM:$memStr"
Write-Output "GPU:$gpuStr"
Write-Output "DISP:$dispStr"
"#;

    let mut cmd = std::process::Command::new("powershell");
    cmd.args(["-NoProfile", "-Command", ps_script]);

    // Hide the PowerShell console window so it doesn't flash on screen.
    #[cfg(target_os = "windows")]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x08000000;
        cmd.creation_flags(CREATE_NO_WINDOW);
    }

    let output = cmd.output();

    let mut mainboard = "Unknown".to_string();
    let mut memory_summary = "Unknown".to_string();
    let mut graphics = "Unknown".to_string();
    let mut display = "Unknown".to_string();

    if let Ok(out) = output {
        let stdout = String::from_utf8_lossy(&out.stdout);
        for line in stdout.lines() {
            let line = line.trim();
            if let Some(val) = line.strip_prefix("BOARD:") {
                mainboard = val.trim().to_string();
            } else if let Some(val) = line.strip_prefix("MEM:") {
                memory_summary = val.trim().to_string();
            } else if let Some(val) = line.strip_prefix("GPU:") {
                graphics = val.trim().to_string();
            } else if let Some(val) = line.strip_prefix("DISP:") {
                display = val.trim().to_string();
            }
        }
    }

    SystemSpecs {
        cpu_name,
        mainboard,
        memory_summary,
        graphics,
        display,
    }
}
