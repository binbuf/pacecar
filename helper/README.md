# helper/

The optional elevated sensor helper `Pacecar.Sensors` (task T16).

Per [../docs/design/01-architecture.md](../docs/design/01-architecture.md),
[../docs/design/06-security-distribution.md](../docs/design/06-security-distribution.md), and ADR
[0021](../docs/design/adr/0021-sensor-helper-and-ipc.md), this is a **separate, self-contained .NET 8
helper** built on PawnIO-backed LibreHardwareMonitor (MPL-2.0). It is intentionally **not** part of
the native C++/vcpkg/static-CRT build: `helper/Directory.Build.props` (empty) stops MSBuild from
walking up into the native conventions, and the project is not in `pacecar.slnx`.

```
helper/
  Directory.Build.props           # empty: keeps the managed helper out of the native conventions
  Pacecar.Sensors/
    Pacecar.Sensors.csproj        # net8.0-windows, self-contained, LibreHardwareMonitorLib 0.9.6
    Program.cs                    # entry point; default DLL dirs; on-demand elevated
    IpcProtocol.cs                # managed mirror of Pacecar.Core/Metrics/IpcProtocol.h
    PipeServer.cs                 # secured pipe: explicit DACL + PIPE_REJECT_REMOTE_CLIENTS
    PipeSecurity.cs               # SDDL builder (denies NETWORK/Anonymous; grants user + Admins)
    ClientValidator.cs            # pid -> image path; impersonate + token-user check
    SensorServer.cs               # LibreHardwareMonitor acquisition (never WinRing0)
    PawnIOProbe.cs                # detect \\.\GLOBALROOT\Device\PawnIO / uninstall key
    app.manifest                  # requestedExecutionLevel=requireAdministrator (single prompt)
    THIRD_PARTY_NOTICES.md        # MPL-2.0 / LGPL-2.1 notices
```

Build and publish (independent of the native `verify.ps1` gate):

```
dotnet build   helper/Pacecar.Sensors/Pacecar.Sensors.csproj -c Release
dotnet publish helper/Pacecar.Sensors/Pacecar.Sensors.csproj -c Release
```

The UI launches the published `Pacecar.Sensors.exe` on demand via `runas` when
`sensors.deep_sensors` is enabled; the protocol is deployment-independent so a Windows service
(LocalSystem) can be added later without a wire change.