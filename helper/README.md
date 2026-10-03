# helper/

Reserved for the optional elevated sensor helper `Pacecar.Sensors`, introduced in task T16.

Per [../docs/design/01-architecture.md](../docs/design/01-architecture.md) and
[../docs/design/08-project-layout-and-testing.md](../docs/design/08-project-layout-and-testing.md),
the helper is a separate, self-contained .NET/LibreHardwareMonitor process (or the documented native
PawnIO CPU-package-temperature fallback). It is intentionally **not** part of the native C++/vcpkg/
static-CRT build; when added it will supply its own `helper/Directory.Build.props` to stop inheriting
the repo-root-less `build/Directory.Build.props` conventions.

This folder exists now so the repository layout is stable.