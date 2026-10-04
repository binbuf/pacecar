using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Session;

namespace Pacecar.Sensors;

/// <summary>
/// Opt-in real-time ETW capture of present events (task T17). Runs only in this elevated helper,
/// never on the UI thread and never in the UI process. It consumes the same providers PresentMon
/// uses (DXGI, D3D9, DxgKrnl, DWM), filters to a single target PID, and exposes the decoded present
/// timestamps for the native side to turn into intervals/FPS.
///
/// This is not DLL injection or API hooking: it is a read-only ETW consumer. Accuracy caveats:
/// Hardware-Accelerated GPU Scheduling makes GPU execution timestamps less accurate, and
/// OpenGL/Vulkan titles are weakly instrumented, so their presents may be missing or late. Only the
/// present-to-present interval is derived here; CPU/GPU frame durations are -1 (unknown) because
/// they require provider-payload correlation that is deliberately out of scope for this slice.
/// </summary>
internal sealed class EtwFrameTime : IDisposable
{
    // PresentMon's provider GUIDs. The DXGI and D3D9 present events are the portable present source;
    // DxgKrnl/DWM are enabled so a future slice can correlate GPU work and composed presents.
    private static readonly Guid DxgiProvider = new("ca11c036-0102-4a2d-a6ad-50a63baf81bc");
    private static readonly Guid D3d9Provider = new("783aca0a-790e-4d7f-8451-aa850511c6b9");
    private static readonly Guid DxgKrnlProvider = new("802ec45a-1e99-4b83-9920-87c98277ba9d");
    private static readonly Guid DwmProvider = new("9e9bba3c-2e38-40cb-99f4-9e8281425164");

    // Present_Start manifest event ids (present to the monitor).
    private const int DxgiPresentStartId = 42;
    private const int D3d9PresentStartId = 1;

    // The native processor receives these ticks; microsecond precision is ample for frame times.
    private const ulong QpcFrequency = 1_000_000;
    private const int MaxQueuedEvents = 4096;

    private readonly object _lock = new();
    private readonly Queue<IpcProtocol.FrameEvent> _pending = new();

    private TraceEventSession? _session;
    private Thread? _thread;

    public uint State { get; private set; } = IpcProtocol.CaptureStateNotCapturing;
    public uint TargetPid { get; private set; }
    public ulong ClockFrequency => QpcFrequency;

    public void Start(uint pid)
    {
        Stop();
        if (pid == 0)
        {
            State = IpcProtocol.CaptureStateNoTarget;
            return;
        }

        if (TraceEventSession.IsElevated() != true)
        {
            State = IpcProtocol.CaptureStateAccessDenied;
            Console.WriteLine("[fps] not elevated; ETW capture unavailable");
            return;
        }

        try
        {
            var session = new TraceEventSession("Pacecar.FrameTime")
            {
                StopOnDispose = true,
            };
            session.EnableProvider(DxgiProvider, TraceEventLevel.Verbose);
            session.EnableProvider(D3d9Provider, TraceEventLevel.Verbose);
            session.EnableProvider(DxgKrnlProvider, TraceEventLevel.Informational);
            session.EnableProvider(DwmProvider, TraceEventLevel.Informational);
            session.Source.AllEvents += data => OnEvent(data, pid);

            _session = session;
            TargetPid = pid;
            State = IpcProtocol.CaptureStateCapturing;

            _thread = new Thread(() =>
            {
                try
                {
                    session.Source.Process();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[fps] ETW processing stopped: {ex.Message}");
                }
            })
            {
                IsBackground = true,
                Name = "Pacecar.FrameTime",
            };
            _thread.Start();
            Console.WriteLine($"[fps] capturing pid {pid}");
        }
        catch (UnauthorizedAccessException ex)
        {
            State = IpcProtocol.CaptureStateAccessDenied;
            Console.WriteLine($"[fps] access denied: {ex.Message}");
        }
        catch (Exception ex)
        {
            // A session-name clash (another tool already owns it) lands here; do not retry.
            State = LooksLikeConflict(ex) ? IpcProtocol.CaptureStateSessionBusy
                                          : IpcProtocol.CaptureStateError;
            Console.WriteLine($"[fps] start failed: {ex.Message}");
            DisposeSession();
        }
    }

    public void Stop()
    {
        DisposeSession();
        lock (_lock)
        {
            _pending.Clear();
        }
        TargetPid = 0;
        if (State == IpcProtocol.CaptureStateCapturing)
        {
            State = IpcProtocol.CaptureStateNotCapturing;
        }
    }

    /// <summary>Removes and returns up to <paramref name="max"/> decoded present events.</summary>
    public List<IpcProtocol.FrameEvent> Drain(int max)
    {
        var result = new List<IpcProtocol.FrameEvent>();
        lock (_lock)
        {
            while (result.Count < max && _pending.Count > 0)
            {
                result.Add(_pending.Dequeue());
            }
        }
        return result;
    }

    public void Dispose() => Stop();

    private void OnEvent(TraceEvent data, uint pid)
    {
        if (data.ProcessID != (int)pid)
        {
            return;
        }

        var guid = data.ProviderGuid;
        var id = (int)data.ID;
        var isPresent = (guid == DxgiProvider && id == DxgiPresentStartId) ||
                        (guid == D3d9Provider && id == D3d9PresentStartId);
        if (!isPresent)
        {
            return;
        }

        // TimeStampRelativeMSec is monotonic within the session and has sub-millisecond resolution,
        // which is what a present-to-present interval needs.
        var microseconds = (ulong)Math.Max(0.0, data.TimeStampRelativeMSec * 1000.0);
        var frameEvent = new IpcProtocol.FrameEvent(microseconds, -1, -1, pid,
                                                    IpcProtocol.FrameEventKindPresent);
        lock (_lock)
        {
            if (_pending.Count >= MaxQueuedEvents)
            {
                _pending.Dequeue();
            }
            _pending.Enqueue(frameEvent);
        }
    }

    private void DisposeSession()
    {
        var session = _session;
        _session = null;
        if (session != null)
        {
            try
            {
                session.Source.StopProcessing();
            }
            catch (Exception)
            {
                // Best effort; disposal below releases the session regardless.
            }
            try
            {
                session.Dispose();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[fps] session dispose failed: {ex.Message}");
            }
        }
        if (_thread != null && _thread.IsAlive)
        {
            _thread.Join(TimeSpan.FromSeconds(2));
        }
        _thread = null;
    }

    private static bool LooksLikeConflict(Exception ex)
    {
        var text = ex.Message;
        return text.Contains("already", StringComparison.OrdinalIgnoreCase) ||
               text.Contains("exists", StringComparison.OrdinalIgnoreCase);
    }
}