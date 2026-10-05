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
    // The DXGI GUID is the registered Microsoft-Windows-DXGI provider (confirmed via
    // `logman query providers` / `wevtutil gp Microsoft-Windows-DXGI`); a wrong GUID silently
    // enables nothing and no present events ever arrive (the FPS readout stays "--").
    private static readonly Guid DxgiProvider = new("ca11c036-0102-4a2d-a6ad-f03cfed5d3c9");
    private static readonly Guid D3d9Provider = new("783aca0a-790e-4d7f-8451-aa850511c6b9");
    private static readonly Guid DxgKrnlProvider = new("802ec45a-1e99-4b83-9920-87c98277ba9d");
    private static readonly Guid DwmProvider = new("9e9bba3c-2e38-40cb-99f4-9e8281425164");

    // Present_Start manifest event ids (present to the monitor).
    private const int DxgiPresentStartId = 42;
    private const int D3d9PresentStartId = 1;

    // Present is tracked per swap chain because a process can own several (a game plus its overlay, a
    // launcher, ...): differencing presents across chains produces meaningless intervals. The swap
    // chain identity is read by name from the event payload because the exact field differs per
    // provider and manifest version.
    private const string SessionName = "Pacecar.FrameTime";
    private const int MaxQueuedEvents = 4096;

    private readonly object _lock = new();
    private readonly Queue<IpcProtocol.FrameEvent> _pending = new();
    private readonly ulong _qpcFrequency;

    private TraceEventSession? _session;
    private Thread? _thread;
    private long _queueDrops;
    private long _eventsSeen;
    private long _presentsSeen;

    // The helper runs hidden, so console output is invisible; HelperLog mirrors it to a file.
    private static void Log(string message) => HelperLog.Write(message);

    public EtwFrameTime()
    {
        // QueryPerformanceFrequency is constant for the machine and matches the units of
        // TraceEvent.TimeStampQPC, which is what we forward to the native processor.
        _qpcFrequency = NativeMethods.QueryPerformanceFrequency(out var frequency) && frequency > 0
            ? (ulong)frequency
            : 10_000_000UL;
    }

    public uint State { get; private set; } = IpcProtocol.CaptureStateNotCapturing;
    public uint TargetPid { get; private set; }
    public ulong ClockFrequency => _qpcFrequency;

    /// <summary>Presents the helper discarded because its local queue overflowed.</summary>
    public uint QueueDrops => (uint)Interlocked.Read(ref _queueDrops);

    /// <summary>ETW events the session itself dropped (capture-health signal).</summary>
    public uint EventsLost
    {
        get
        {
            var session = _session;
            if (session is null)
            {
                return 0;
            }
            try
            {
                return (uint)session.EventsLost;
            }
            catch (Exception)
            {
                return 0;
            }
        }
    }

    public void Start(uint pid)
    {
        Stop();
        Log($"[fps] Start requested pid={pid}");
        if (pid == 0)
        {
            State = IpcProtocol.CaptureStateNoTarget;
            Log("[fps] no target pid; not capturing");
            return;
        }

        if (TraceEventSession.IsElevated() != true)
        {
            State = IpcProtocol.CaptureStateAccessDenied;
            Log("[fps] not elevated; ETW capture unavailable");
            return;
        }

        Interlocked.Exchange(ref _queueDrops, 0);
        Interlocked.Exchange(ref _eventsSeen, 0);
        Interlocked.Exchange(ref _presentsSeen, 0);
        try
        {
            // A real-time ETW session outlives the process that created it if that process is killed
            // without disposing (the UI terminates the helper with TerminateProcess), so a previous
            // run can leave a logger that blocks every later start. Stop any lingering one first.
            StopLingeringSession();

            var session = new TraceEventSession(SessionName)
            {
                StopOnDispose = true,
            };
            session.EnableProvider(DxgiProvider, TraceEventLevel.Verbose);
            session.EnableProvider(D3d9Provider, TraceEventLevel.Verbose);
            session.EnableProvider(DxgKrnlProvider, TraceEventLevel.Informational);
            session.EnableProvider(DwmProvider, TraceEventLevel.Informational);
            session.Source.AllEvents += data => OnEvent(data, pid);
            Log("[fps] session opened; DXGI/D3D9/DxgKrnl/DWM providers enabled");

            _session = session;
            TargetPid = pid;
            State = IpcProtocol.CaptureStateCapturing;

            _thread = new Thread(() =>
            {
                try
                {
                    session.Source.Process();
                    Log("[fps] ETW processing ended normally");
                }
                catch (Exception ex)
                {
                    Log($"[fps] ETW processing stopped: {ex}");
                }
            })
            {
                IsBackground = true,
                Name = "Pacecar.FrameTime",
            };
            _thread.Start();
            Log($"[fps] capturing pid {pid}");
        }
        catch (UnauthorizedAccessException ex)
        {
            State = IpcProtocol.CaptureStateAccessDenied;
            Log($"[fps] access denied: {ex.Message}");
        }
        catch (Exception ex)
        {
            // A session-name clash (another tool already owns it) lands here; do not retry.
            State = LooksLikeConflict(ex) ? IpcProtocol.CaptureStateSessionBusy
                                          : IpcProtocol.CaptureStateError;
            Log($"[fps] start failed: {ex}");
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
        var seen = Interlocked.Increment(ref _eventsSeen);
        if (seen <= 5 || seen % 5000 == 0)
        {
            Log($"[fps] event provider={data.ProviderName} id={data.ID} eventPid={data.ProcessID} " +
                $"target={pid} seen={seen}");
        }

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

        var presents = Interlocked.Increment(ref _presentsSeen);
        if (presents <= 3 || presents % 1000 == 0)
        {
            Log($"[fps] present id={id} swapChain-readable={data.PayloadNames?.Length > 0} " +
                $"presents={presents}");
        }

        // TimeStampQPC is the raw QueryPerformanceCounter value (session-relative) and is exactly the
        // timebase the native processor is told about via ClockFrequency, so no lossy unit conversion
        // happens in the helper. TraceEvent discourages it in favor of TimeStampRelativeMSec, but the
        // raw counter avoids the double round-trip that costs sub-millisecond precision late in a
        // long session.
#pragma warning disable CS0618
        var qpc = data.TimeStampQPC;
#pragma warning restore CS0618
        if (qpc == 0)
        {
            // Some sessions/traces do not carry a raw QPC value; synthesise monotonic ticks from the
            // relative milliseconds using the same frequency so the native math stays consistent.
            qpc = (long)(data.TimeStampRelativeMSec * _qpcFrequency / 1000.0);
        }
        var qpcTicks = (ulong)Math.Max(0L, qpc);

        TryReadSwapChain(data, out var swapChain);

        var frameEvent = new IpcProtocol.FrameEvent(qpcTicks, -1, -1, swapChain, pid,
                                                    IpcProtocol.FrameEventKindPresent);
        lock (_lock)
        {
            if (_pending.Count >= MaxQueuedEvents)
            {
                _pending.Dequeue();
                Interlocked.Increment(ref _queueDrops);
            }
            _pending.Enqueue(frameEvent);
        }
    }

    private static void TryReadSwapChain(TraceEvent data, out ulong value)
    {
        value = 0;
        try
        {
            // Match any payload field naming the swap chain (DXGI uses `pIDXGISwapChain`, D3D9 uses
            // `pSwapChain`, and manifests have shifted these before), so a provider change degrades
            // to "unknown" (0, a single stream) rather than mis-attributing presents.
            foreach (var name in data.PayloadNames)
            {
                if (name.Contains("SwapChain", StringComparison.OrdinalIgnoreCase))
                {
                    var raw = data.PayloadByName(name);
                    if (raw is not null)
                    {
                        value = Convert.ToUInt64(raw);
                        return;
                    }
                }
            }
        }
        catch (Exception)
        {
            value = 0;
        }
    }

    private static void StopLingeringSession()
    {
        try
        {
            using var existing = TraceEventSession.GetActiveSession(SessionName);
            if (existing is not null)
            {
                existing.Stop();
                Log("[fps] stopped a lingering ETW session");
            }
        }
        catch (Exception ex)
        {
            Log($"[fps] stale-session cleanup failed: {ex.Message}");
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
                Log($"[fps] session dispose failed: {ex.Message}");
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