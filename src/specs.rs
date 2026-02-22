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

$gpu = Get-CimInstance Win32_VideoController | Select-Object -First 1
$vramGB = [math]::Round($gpu.AdapterRAM / 1GB)
if ($vramGB -gt 0) {
    $gpuStr = "$($gpu.Name) ($vramGB GB)"
} else {
    $gpuStr = "$($gpu.Name)"
}

$vc = Get-CimInstance Win32_VideoController | Select-Object -First 1
$hres = $vc.CurrentHorizontalResolution
$vres = $vc.CurrentVerticalResolution
$hz = $vc.CurrentRefreshRate
$dispStr = "$hres x $vres @ $hz Hz"

Write-Output "BOARD:$boardStr"
Write-Output "MEM:$memStr"
Write-Output "GPU:$gpuStr"
Write-Output "DISP:$dispStr"
"#;

    let output = std::process::Command::new("powershell")
        .args(["-NoProfile", "-Command", ps_script])
        .output();

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
