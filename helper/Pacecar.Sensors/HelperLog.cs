namespace Pacecar.Sensors;

/// <summary>
/// Tiny diagnostic log for the helper. The helper is launched hidden (SW_HIDE), so Console output is
/// invisible; mirroring to a file lets a user share what actually happened. Writes are best-effort
/// and must never affect the sensor/capture path.
/// </summary>
internal static class HelperLog
{
    private static readonly object Lock = new();

    public static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Pacecar",
        "helper.log");

    public static void Write(string message)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {message}";
        Console.WriteLine(line);
        try
        {
            var directory = Path.GetDirectoryName(FilePath)!;
            Directory.CreateDirectory(directory);
            lock (Lock)
            {
                File.AppendAllText(FilePath, line + Environment.NewLine);
            }
        }
        catch (Exception)
        {
            // Diagnostics must never take down the helper.
        }
    }
}