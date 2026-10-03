# Cross-process click-through probe for the pacecar overlay (task T09).
#
# Measures whether mouse hit-testing passes through the overlay to a window of ANOTHER process,
# for both recipes and both Recipe B strategies. It is the automated substitute for the manual
# "click over a windowed app" check: this session has no interactive input desktop (synthetic
# mouse input is ignored), so instead of injecting clicks it asks USER32 which window owns the
# point (`WindowFromPoint`), the same hit-test that routes mouse input. `WindowFromPoint` skips
# windows carrying `WS_EX_TRANSPARENT`, which is exactly the documented pass-through mechanism.
#
# Usage:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/clickthrough-probe.ps1
#   ... -Exe out\x64\Release\Pacecar.Overlay.exe -Out report.txt
#
# Exit code 0 when every scenario produced the expected result, 1 otherwise. Build Release first:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release

[CmdletBinding()]
param(
    [string]$Exe = "out\x64\Release\Pacecar.Overlay.exe",
    [int]$X = 120,
    [int]$Y = 120,
    [int]$Width = 360,
    [int]$Height = 240,
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "overlay executable not found: $Exe (build Release first)"
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$X = [Math]::Max(0, $X)
$Y = [Math]::Max(0, $Y)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class Native
{
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hWnd, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    public const int GWL_EXSTYLE = -20;
    public const uint WM_NCHITTEST = 0x0084;
    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_NOACTIVATE = 0x0010;
    public const uint SWP_SHOWWINDOW = 0x0040;

    public static IntPtr FindTopLevelWindow(uint pid, string className)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) =>
        {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner != pid) return true;
            var sb = new StringBuilder(256);
            GetClassName(h, sb, sb.Capacity);
            if (sb.ToString() == className)
            {
                found = h;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string ClassName(IntPtr hWnd)
    {
        if (hWnd == IntPtr.Zero) return "";
        var sb = new StringBuilder(256);
        GetClassName(hWnd, sb, sb.Capacity);
        return sb.ToString();
    }

    public static uint ProcessId(IntPtr hWnd)
    {
        uint pid;
        GetWindowThreadProcessId(hWnd, out pid);
        return pid;
    }

    public static int ExStyle(IntPtr hWnd) { return GetWindowLong(hWnd, GWL_EXSTYLE); }

    public static int NcHitTest(IntPtr hWnd, int x, int y)
    {
        IntPtr packed = (IntPtr)((y << 16) | (x & 0xFFFF));
        return (int)SendMessage(hWnd, WM_NCHITTEST, IntPtr.Zero, packed);
    }
}
"@

# A plain target window owned by THIS probe process; the overlay lives in another process.
$target = New-Object System.Windows.Forms.Form
$target.Text = "pacecar click-through target"
$target.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::Sizable
$target.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$target.ShowInTaskbar = $false
$target.ClientSize = New-Object System.Drawing.Size($Width, $Height)
$target.BackColor = [System.Drawing.Color]::FromArgb(30, 90, 160)
$target.TopMost = $false
$target.Show()
Start-Sleep -Milliseconds 200
[System.Windows.Forms.Application]::DoEvents()
$targetHwnd = $target.Handle

function Set-TargetRect([int]$x, [int]$y, [int]$w, [int]$h) {
    $target.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::Sizable
    [void][Native]::SetWindowPos($targetHwnd, [IntPtr]::Zero, $x, $y, $w, $h,
        ([Native]::SWP_NOZORDER -bor [Native]::SWP_NOACTIVATE -bor [Native]::SWP_SHOWWINDOW))
}

# Borderless "fullscreen-windowed" target: covers the whole primary monitor like a borderless/FSO
# game (true exclusive fullscreen outranks a normal topmost window and needs uiAccess, out of scope).
function Set-TargetBorderlessFullscreen() {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $target.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
    [void][Native]::SetWindowPos($targetHwnd, [IntPtr]::Zero, $bounds.X, $bounds.Y,
        $bounds.Width, $bounds.Height,
        ([Native]::SWP_NOZORDER -bor [Native]::SWP_NOACTIVATE -bor [Native]::SWP_SHOWWINDOW))
}

function Wait-Overlay([uint32]$procId, [int]$timeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    while ((Get-Date) -lt $deadline) {
        [System.Windows.Forms.Application]::DoEvents()
        $hwnd = [Native]::FindTopLevelWindow($procId, "PacecarOverlayWindow")
        if ($hwnd -ne [IntPtr]::Zero -and [Native]::IsWindowVisible($hwnd)) { return $hwnd }
        Start-Sleep -Milliseconds 50
    }
    return [IntPtr]::Zero
}

# Assert: Recipe A click-through must pass through and interactive must not. Recipe B is recorded
# (pass or fail): the design only requires the cross-process result to be measured and the fallback
# to Recipe A documented.
$scenarios = @(
    [pscustomobject]@{ Name = "A click-through over windowed target"; Args = @("--recipe=a", "--click-through"); PassThrough = $true;  HitTest = $false; Assert = $true;  Fullscreen = $false },
    [pscustomobject]@{ Name = "A click-through over borderless-fullscreen target"; Args = @("--recipe=a", "--click-through"); PassThrough = $true; HitTest = $false; Assert = $true; Fullscreen = $true },
    [pscustomobject]@{ Name = "A interactive over windowed target";   Args = @("--recipe=a", "--interactive");   PassThrough = $false; HitTest = $false; Assert = $true;  Fullscreen = $false },
    [pscustomobject]@{ Name = "B exstyle (default)";     Args = @("--recipe=b", "--click-through");              PassThrough = $true;  HitTest = $false; Assert = $false; Fullscreen = $false },
    [pscustomobject]@{ Name = "B hit-test (HTTRANSPARENT)"; Args = @("--recipe=b", "--click-through", "--hit-test"); PassThrough = $false; HitTest = $true;  Assert = $false; Fullscreen = $false }
)

$lines = New-Object System.Collections.Generic.List[string]
$pass = $true
$lines.Add("pacecar cross-process click-through probe (WindowFromPoint hit test)")
$lines.Add("exe=$Exe rect=$X,$Y,$Width,$Height  probePid=$PID targetHwnd=$targetHwnd")
$lines.Add("")

foreach ($scenario in $scenarios) {
    if ($scenario.Fullscreen) { Set-TargetBorderlessFullscreen } else { Set-TargetRect -x $X -y $Y -w $Width -h $Height }
    [System.Windows.Forms.Application]::DoEvents()
    Start-Sleep -Milliseconds 150

    $arguments = @($scenario.Args) + @("--position=$X,$Y,$Width,$Height")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru
    try {
        $overlayHwnd = Wait-Overlay -procId $process.Id -timeoutMs 8000
        if ($overlayHwnd -eq [IntPtr]::Zero) {
            $lines.Add("$($scenario.Name): ERROR overlay window not found")
            $pass = $false
            continue
        }

        # Align a windowed target exactly under the overlay using physical pixels; a fullscreen
        # target already covers the overlay point.
        $rect = New-Object Native+RECT
        [void][Native]::GetWindowRect($overlayHwnd, [ref]$rect)
        $cx = [int](($rect.Left + $rect.Right) / 2)
        $cy = [int](($rect.Top + $rect.Bottom) / 2)
        if (-not $scenario.Fullscreen) {
            Set-TargetRect -x $rect.Left -y $rect.Top -w ($rect.Right - $rect.Left) -h ($rect.Bottom - $rect.Top)
        }
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 250
        [System.Windows.Forms.Application]::DoEvents()

        $point = New-Object Native+POINT
        $point.X = $cx
        $point.Y = $cy
        $hit = [Native]::WindowFromPoint($point)
        $hitIsOverlay = ($hit -eq $overlayHwnd)
        $hitPid = [Native]::ProcessId($hit)
        $hitClass = [Native]::ClassName($hit)
        $exLong = [uint32][Native]::ExStyle($overlayHwnd)
        $styleBits = @()
        foreach ($bit in @(
            @{ Mask = 0x00080000; Name = "LAYERED" },
            @{ Mask = 0x00000020; Name = "TRANSPARENT" },
            @{ Mask = 0x00000080; Name = "TOOLWINDOW" },
            @{ Mask = 0x08000000; Name = "NOACTIVATE" },
            @{ Mask = 0x00000008; Name = "TOPMOST" },
            @{ Mask = 0x00200000; Name = "NOREDIRECTIONBITMAP" })) {
            if (($exLong -band $bit.Mask) -ne 0) { $styleBits += $bit.Name }
        }
        $exStyle = "0x{0:X8}" -f $exLong
        $hitTest = if ($scenario.HitTest) {
            $result = [Native]::NcHitTest($overlayHwnd, $cx, $cy)
            if ($result -eq -1) { "HTTRANSPARENT" } else { "0x{0:X}" -f $result }
        } else { "n/a" }

        # Expected: pass-through scenarios must NOT hit the overlay; interactive/hit-test must.
        $expectedHit = if ($scenario.PassThrough) { $false } else { $true }
        $ok = ($hitIsOverlay -eq $expectedHit)
        if ($scenario.Assert -and (-not $ok)) { $pass = $false }

        $verdict = if (-not $scenario.Assert) { "RECORDED" } elseif ($ok) { "OK" } else { "UNEXPECTED" }
        $lines.Add(("{0}: {1} (cross-process pass-through={2})" -f
                    $scenario.Name, $verdict, $(if ($hitIsOverlay) { "no" } else { "yes" })))
        $lines.Add(("  overlayHwnd={0} overlayPid={1} exStyle={2} [{3}]" -f
                    $overlayHwnd, $process.Id, $exStyle, ($styleBits -join "|")))
        $lines.Add(("  WindowFromPoint=0x{0:X} pid={1} class='{2}'  -> {3}" -f
                    ([int64]$hit), $hitPid, $hitClass,
                    $(if ($hitIsOverlay) { "would press the overlay" } else { "passes through to window beneath" })))
        $lines.Add(("  WM_NCHITTEST on overlay={0}; expected cross-process pass-through={1}" -f
                    $hitTest, ($(if ($scenario.PassThrough) { "yes" } else { "no" }))))
        $lines.Add("")
    }
    finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(2000) | Out-Null }
    }
}

$target.Close()
[System.Windows.Forms.Application]::DoEvents()

$lines.Add($(if ($pass) { "RESULT: PASS - every asserted scenario matched the expected hit test (Recipe B recorded above)" } else { "RESULT: FAIL - see UNEXPECTED scenarios above" }))
$report = ($lines -join [Environment]::NewLine)
Write-Output $report
if ($Out -ne "") { Set-Content -LiteralPath $Out -Value $report -Encoding utf8 }

exit $(if ($pass) { 0 } else { 1 })