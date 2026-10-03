# Third-party notices — Pacecar.Sensors

Pacecar ships no kernel driver. Deep sensors use the user-installed, signed **PawnIO** driver. The
helper loads only already-signed PawnIO modules; **WinRing0 is never bundled or used**.

## LibreHardwareMonitor (LibreHardwareMonitorLib)

- License: **Mozilla Public License 2.0 (MPL-2.0)**
- Project: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
- Used for: CPU/board/DIMM/disk temperature and fan RPM acquisition on top of PawnIO.
- Version pinned in `Pacecar.Sensors.csproj` (a PawnIO-based release; WinRing0 shipped through
  v0.9.4 is explicitly excluded).

## PawnIO

- Driver: proprietary, installed separately by the user (https://pawnio.eu). Pacecar does not
  redistribute the driver.
- Modules loaded by LibreHardwareMonitor (for example `IntelMSR.bin`, `RyzenSMU.bin`, `LpcIO.bin`,
  `Smbus*.bin`): **GNU Lesser General Public License 2.1 (LGPL-2.1)**, redistributable. Keep this
  notice when redistributing the modules.

## Other managed dependencies

Transitive dependencies of LibreHardwareMonitorLib (for example HidSharp, System.Management) carry
their own licenses; see the restored NuGet packages under `~/.nuget/packages` for exact terms.