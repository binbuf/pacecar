using LibreHardwareMonitor.Hardware;

namespace Pacecar.Sensors;

/// <summary>
/// Deep-sensor acquisition via LibreHardwareMonitor (PawnIO-backed; MPL-2.0). LHM owns the
/// board/Super-I/O/DIMM intelligence that would be a multi-month effort to reimplement natively.
/// The helper is the isolated privileged component, so the managed dependency stays out of the
/// native UI.
/// </summary>
internal sealed class SensorServer : IDisposable
{
    private readonly Computer _computer = new()
    {
        IsCpuEnabled = true,
        IsMotherboardEnabled = true,
        IsMemoryEnabled = true,
        IsStorageEnabled = true,
        IsControllerEnabled = true,
        IsGpuEnabled = true,
    };

    private bool _opened;

    public void Open()
    {
        if (_opened)
        {
            return;
        }

        _computer.Open();
        _opened = true;
    }

    public IReadOnlyList<IpcProtocol.Reading> Read()
    {
        if (!_opened)
        {
            return Array.Empty<IpcProtocol.Reading>();
        }

        var readings = new List<IpcProtocol.Reading>();
        foreach (var hardware in _computer.Hardware)
        {
            Visit(hardware, readings);
        }

        return readings;
    }

    public void Dispose()
    {
        if (_opened)
        {
            _computer.Close();
            _opened = false;
        }
    }

    private static void Visit(IHardware hardware, List<IpcProtocol.Reading> readings)
    {
        try
        {
            hardware.Update();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[sensors] update failed for {hardware.Name}: {ex.Message}");
            return;
        }

        var coreTemperatureIndex = 0;
        var dimmTemperatureIndex = 0;
        foreach (var sensor in hardware.Sensors)
        {
            if (sensor.Value is not { } value || float.IsNaN(value))
            {
                continue;
            }

            switch (sensor.SensorType)
            {
                case SensorType.Temperature:
                    readings.Add(MapTemperature(hardware.HardwareType, sensor, value,
                        ref coreTemperatureIndex, ref dimmTemperatureIndex));
                    break;
                case SensorType.Fan:
                    readings.Add(new IpcProtocol.Reading(IpcProtocol.SensorIdFanRpm, true,
                        sensor.Index, value));
                    break;
                default:
                    break;
            }
        }

        foreach (var sub in hardware.SubHardware)
        {
            Visit(sub, readings);
        }
    }

    private static IpcProtocol.Reading MapTemperature(HardwareType type, ISensor sensor, float value,
        ref int coreTemperatureIndex, ref int dimmTemperatureIndex)
    {
        ushort id;
        int index;
        switch (type)
        {
            case HardwareType.Cpu when IsPackageTemperature(sensor.Name):
                id = IpcProtocol.SensorIdCpuPackageTemperature;
                index = 0;
                break;
            case HardwareType.Cpu:
                id = IpcProtocol.SensorIdCpuCoreTemperature;
                index = coreTemperatureIndex++;
                break;
            case HardwareType.Memory:
                id = IpcProtocol.SensorIdDimmTemperature;
                index = dimmTemperatureIndex++;
                break;
            case HardwareType.Storage:
                id = IpcProtocol.SensorIdDiskTemperature;
                index = sensor.Index;
                break;
            case HardwareType.GpuNvidia:
            case HardwareType.GpuAmd:
            case HardwareType.GpuIntel:
                id = IpcProtocol.SensorIdGpuTemperature;
                index = sensor.Index;
                break;
            case HardwareType.Motherboard:
            case HardwareType.SuperIO:
                id = IpcProtocol.SensorIdMainboardTemperature;
                index = sensor.Index;
                break;
            default:
                id = IpcProtocol.SensorIdUnknown;
                index = sensor.Index;
                break;
        }

        return new IpcProtocol.Reading(id, true, index, value);
    }

    private static bool IsPackageTemperature(string name)
    {
        return name.Contains("package", StringComparison.OrdinalIgnoreCase) ||
               name.Contains("tctl", StringComparison.OrdinalIgnoreCase) ||
               name.Contains("tdie", StringComparison.OrdinalIgnoreCase);
    }
}