using System;
using System.IO;
using System.Runtime.InteropServices;
using LibreHardwareMonitor.Hardware;

namespace HwMonShim;

/// <summary>
/// Flat struct returned by hwmon_cpu_temps.
/// Package temp + up to 128 per-core temps.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct CpuTemps
{
    /// Package / Tctl / Tdie temperature, or -1 if unavailable.
    public float PackageTemp;
    /// Number of valid entries in CoreTemps.
    public int CoreCount;
    /// Per-core temperatures. Unused slots are 0.
    public fixed float CoreTemps[128];
}

/// <summary>
/// Flat struct returned by hwmon_disk_temps.
/// Up to 32 disk drives with name + temperature.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DiskTemps
{
    /// Number of valid entries.
    public int DiskCount;
    /// Per-disk temperatures. Unused slots are 0.
    public fixed float Temps[32];
    /// Per-disk names, each slot is 128 bytes (UTF-8, null-padded).
    public fixed byte Names[32 * 128];
}

/// <summary>
/// Flat struct returned by hwmon_fan_speeds.
/// Up to 16 fan speed readings from SuperIO (motherboard sub-hardware).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct FanSpeeds
{
    public int FanCount;
    public fixed float Rpms[16];
    public fixed byte Names[16 * 128];
}

/// <summary>
/// Flat struct returned by hwmon_mainboard_temps.
/// Up to 32 temperature readings from motherboard sub-hardware.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MainboardTemps
{
    public int TempCount;
    public fixed float Temps[32];
    public fixed byte Names[32 * 128];
}

/// <summary>
/// Flat struct returned by hwmon_ram_temp.
/// RAM temperature from DIMM sensors (via motherboard SMBus). -1 means unavailable.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct RamTemp
{
    public float Temperature;
}

/// <summary>
/// Flat struct returned by hwmon_gpu_fan_speed.
/// Highest GPU fan RPM, or -1 if unavailable.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct GpuFanSpeed
{
    public float Rpm;
}

public static class HwMon
{
    private static Computer? _computer;
    private static string? _logPath;

    private static void Log(string msg)
    {
        try
        {
            if (_logPath == null)
            {
                var dir = AppContext.BaseDirectory;
                _logPath = Path.Combine(dir, "hwmon-shim.log");
            }
            File.AppendAllText(_logPath, msg + Environment.NewLine);
        }
        catch { }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_init")]
    public static int Init()
    {
        try
        {
            Log("hwmon_init called");

            var computer = new Computer { IsCpuEnabled = true, IsStorageEnabled = true, IsMotherboardEnabled = true, IsMemoryEnabled = true, IsGpuEnabled = true };
            computer.Open();
            _computer = computer;

            Log($"Hardware count: {computer.Hardware.Count}");
            foreach (var hw in computer.Hardware)
            {
                Log($"  Hardware: {hw.Name} Type={hw.HardwareType}");
                hw.Update();
                foreach (var sensor in hw.Sensors)
                    Log($"    Sensor: {sensor.Name} Type={sensor.SensorType} Value={sensor.Value}");
                foreach (var sub in hw.SubHardware)
                {
                    sub.Update();
                    Log($"    SubHardware: {sub.Name} Type={sub.HardwareType}");
                    foreach (var sensor in sub.Sensors)
                        Log($"      Sensor: {sensor.Name} Type={sensor.SensorType} Value={sensor.Value}");
                }
            }

            return 0;
        }
        catch (Exception ex)
        {
            Log($"hwmon_init exception: {ex}");
            return -1;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_cpu_temps")]
    public static unsafe int CpuTempsQuery(CpuTemps* result)
    {
        try
        {
            if (result == null) return -1;

            var computer = _computer;
            if (computer == null) return -1;

            result->PackageTemp = -1f;
            result->CoreCount = 0;

            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.Cpu) continue;

                hardware.Update();
                foreach (var sub in hardware.SubHardware)
                    sub.Update();

                CollectFromSensors(hardware.Sensors, result);

                foreach (var sub in hardware.SubHardware)
                    CollectFromSensors(sub.Sensors, result);

                break;
            }

            return 0;
        }
        catch
        {
            return -1;
        }
    }

    private static unsafe void CollectFromSensors(ISensor[] sensors, CpuTemps* result)
    {
        foreach (var sensor in sensors)
        {
            if (sensor.SensorType != SensorType.Temperature) continue;
            if (sensor.Value == null) continue;

            float val = sensor.Value.Value;
            string name = sensor.Name;

            if (name.Contains("Package", StringComparison.OrdinalIgnoreCase)
                || name.Contains("Tctl", StringComparison.OrdinalIgnoreCase)
                || name.Contains("Tdie", StringComparison.OrdinalIgnoreCase))
            {
                result->PackageTemp = val;
                continue;
            }

            if (name.Contains("Core", StringComparison.OrdinalIgnoreCase))
            {
                int idx = ExtractCoreIndex(name);
                if (idx >= 0 && idx < 128)
                {
                    result->CoreTemps[idx] = val;
                    if (idx + 1 > result->CoreCount)
                        result->CoreCount = idx + 1;
                }
            }
        }
    }

    private static int ExtractCoreIndex(string name)
    {
        int hash = name.LastIndexOf('#');
        if (hash < 0 || hash + 1 >= name.Length) return -1;

        int start = hash + 1;
        int end = start;
        while (end < name.Length && char.IsDigit(name[end])) end++;
        if (end == start) return -1;

        if (int.TryParse(name.AsSpan(start, end - start), out int idx))
            return idx;
        return -1;
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_disk_temps")]
    public static unsafe int DiskTempsQuery(DiskTemps* result)
    {
        try
        {
            if (result == null) return -1;

            var computer = _computer;
            if (computer == null) return -1;

            result->DiskCount = 0;

            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.Storage) continue;

                hardware.Update();

                float temp = -1f;
                foreach (var sensor in hardware.Sensors)
                {
                    if (sensor.SensorType == SensorType.Temperature && sensor.Value != null)
                    {
                        temp = sensor.Value.Value;
                        break;
                    }
                }

                if (temp <= 0f) continue;

                int idx = result->DiskCount;
                if (idx >= 32) break;

                result->Temps[idx] = temp;

                // Write the hardware name into the fixed-size name slot.
                var nameBytes = System.Text.Encoding.UTF8.GetBytes(hardware.Name);
                int len = Math.Min(nameBytes.Length, 127);
                for (int i = 0; i < len; i++)
                    result->Names[idx * 128 + i] = nameBytes[i];
                result->Names[idx * 128 + len] = 0;

                result->DiskCount = idx + 1;
            }

            return 0;
        }
        catch
        {
            return -1;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_fan_speeds")]
    public static unsafe int FanSpeedsQuery(FanSpeeds* result)
    {
        try
        {
            if (result == null) return -1;
            var computer = _computer;
            if (computer == null) return -1;

            result->FanCount = 0;

            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.Motherboard) continue;

                hardware.Update();

                foreach (var sub in hardware.SubHardware)
                {
                    sub.Update();
                    foreach (var sensor in sub.Sensors)
                    {
                        if (sensor.SensorType != SensorType.Fan) continue;
                        if (sensor.Value == null || sensor.Value.Value <= 0f) continue;

                        int idx = result->FanCount;
                        if (idx >= 16) break;

                        result->Rpms[idx] = sensor.Value.Value;

                        var nameBytes = System.Text.Encoding.UTF8.GetBytes(sensor.Name);
                        int len = Math.Min(nameBytes.Length, 127);
                        for (int i = 0; i < len; i++)
                            result->Names[idx * 128 + i] = nameBytes[i];
                        result->Names[idx * 128 + len] = 0;

                        result->FanCount = idx + 1;
                    }
                }
                break;
            }

            return 0;
        }
        catch { return -1; }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_mainboard_temps")]
    public static unsafe int MainboardTempsQuery(MainboardTemps* result)
    {
        try
        {
            if (result == null) return -1;
            var computer = _computer;
            if (computer == null) return -1;

            result->TempCount = 0;

            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.Motherboard) continue;

                hardware.Update();

                foreach (var sub in hardware.SubHardware)
                {
                    sub.Update();
                    foreach (var sensor in sub.Sensors)
                    {
                        if (sensor.SensorType != SensorType.Temperature) continue;
                        if (sensor.Value == null || sensor.Value.Value <= 0f) continue;

                        int idx = result->TempCount;
                        if (idx >= 32) break;

                        result->Temps[idx] = sensor.Value.Value;

                        var nameBytes = System.Text.Encoding.UTF8.GetBytes(sensor.Name);
                        int len = Math.Min(nameBytes.Length, 127);
                        for (int i = 0; i < len; i++)
                            result->Names[idx * 128 + i] = nameBytes[i];
                        result->Names[idx * 128 + len] = 0;

                        result->TempCount = idx + 1;
                    }
                }
                break;
            }

            return 0;
        }
        catch { return -1; }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_ram_temp")]
    public static unsafe int RamTempQuery(RamTemp* result)
    {
        try
        {
            if (result == null) return -1;
            var computer = _computer;
            if (computer == null) return -1;

            result->Temperature = -1f;

            Log("ram_temp: searching for DIMM temperature sensors...");

            // 1) HardwareType.Memory — LHWM natively exposes DDR5 DIMM-TS
            //    temperature sensors here. This is the primary source.
            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.Memory) continue;

                hardware.Update();
                Log($"ram_temp: found Memory hardware: {hardware.Name}");

                foreach (var sensor in hardware.Sensors)
                {
                    Log($"ram_temp:   sensor: {sensor.Name} type={sensor.SensorType} val={sensor.Value}");
                    if (sensor.SensorType == SensorType.Temperature && sensor.Value != null && sensor.Value.Value > 0f)
                    {
                        if (result->Temperature < 0f || sensor.Value.Value > result->Temperature)
                            result->Temperature = sensor.Value.Value;
                    }
                }

                foreach (var sub in hardware.SubHardware)
                {
                    sub.Update();
                    Log($"ram_temp:   sub-hardware: {sub.Name} type={sub.HardwareType}");
                    foreach (var sensor in sub.Sensors)
                    {
                        Log($"ram_temp:     sensor: {sensor.Name} type={sensor.SensorType} val={sensor.Value}");
                        if (sensor.SensorType == SensorType.Temperature && sensor.Value != null && sensor.Value.Value > 0f)
                        {
                            if (result->Temperature < 0f || sensor.Value.Value > result->Temperature)
                                result->Temperature = sensor.Value.Value;
                        }
                    }
                }
            }

            // 2) Fallback: search motherboard sub-hardware for DIMM/DRAM named sensors
            //    (older LHWM versions or boards that expose them via SMBus).
            if (result->Temperature < 0f)
            {
                Log("ram_temp: no Memory temp found, checking motherboard sub-hardware...");
                foreach (var hardware in computer.Hardware)
                {
                    if (hardware.HardwareType != HardwareType.Motherboard) continue;

                    hardware.Update();
                    foreach (var sub in hardware.SubHardware)
                    {
                        sub.Update();
                        foreach (var sensor in sub.Sensors)
                        {
                            if (sensor.SensorType != SensorType.Temperature) continue;
                            if (sensor.Value == null || sensor.Value.Value <= 0f) continue;

                            string nameLower = sensor.Name.ToLowerInvariant();
                            if (nameLower.Contains("dimm") || nameLower.Contains("dram") || nameLower.Contains("memory"))
                            {
                                Log($"ram_temp:   found MB sensor: {sensor.Name} = {sensor.Value.Value}");
                                if (result->Temperature < 0f || sensor.Value.Value > result->Temperature)
                                    result->Temperature = sensor.Value.Value;
                            }
                        }
                    }
                    break;
                }
            }

            Log($"ram_temp: final result = {result->Temperature}");
            return 0;
        }
        catch (Exception ex)
        {
            Log($"ram_temp exception: {ex}");
            return -1;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_gpu_fan_speed")]
    public static unsafe int GpuFanSpeedQuery(GpuFanSpeed* result)
    {
        try
        {
            if (result == null) return -1;
            var computer = _computer;
            if (computer == null) return -1;

            result->Rpm = -1f;

            foreach (var hardware in computer.Hardware)
            {
                if (hardware.HardwareType != HardwareType.GpuNvidia &&
                    hardware.HardwareType != HardwareType.GpuAmd &&
                    hardware.HardwareType != HardwareType.GpuIntel)
                    continue;

                hardware.Update();

                foreach (var sensor in hardware.Sensors)
                {
                    if (sensor.SensorType != SensorType.Fan) continue;
                    if (sensor.Value == null || sensor.Value.Value <= 0f) continue;

                    if (sensor.Value.Value > result->Rpm)
                        result->Rpm = sensor.Value.Value;
                }

                foreach (var sub in hardware.SubHardware)
                {
                    sub.Update();
                    foreach (var sensor in sub.Sensors)
                    {
                        if (sensor.SensorType != SensorType.Fan) continue;
                        if (sensor.Value == null || sensor.Value.Value <= 0f) continue;

                        if (sensor.Value.Value > result->Rpm)
                            result->Rpm = sensor.Value.Value;
                    }
                }
            }

            return 0;
        }
        catch { return -1; }
    }

    [UnmanagedCallersOnly(EntryPoint = "hwmon_shutdown")]
    public static void Shutdown()
    {
        try
        {
            var computer = _computer;
            _computer = null;
            computer?.Close();
        }
        catch { }
    }
}
