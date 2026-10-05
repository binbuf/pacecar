#requires -Version 7
<#
.SYNOPSIS
  Local (CI-less) build helper for the native Pacecar solution.
.DESCRIPTION
  Configures nothing globally: it locates MSBuild and the vcpkg root the same way
  scripts/verify.ps1 does, builds the .slnx for the requested configuration, and
  optionally runs the test executables. Use -Clean to wipe the out/ tree first.
.EXAMPLE
  pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug
.EXAMPLE
  pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$Test,
    # Override the project's pinned MSVC platform toolset (e.g. v143 on a CI image that has no
    # v145). Omitted by default so local builds use the .vcxproj value.
    [string]$PlatformToolset
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

function Get-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'build: MSBuild not found (install the VS C++ workload or put msbuild on PATH).'
}

function Get-VcpkgRoot {
    if ($env:VCPKG_ROOT -and (Test-Path -LiteralPath $env:VCPKG_ROOT)) { return $env:VCPKG_ROOT }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        if ($vsPath) {
            $bundled = Join-Path $vsPath 'VC\vcpkg'
            if (Test-Path -LiteralPath (Join-Path $bundled 'scripts\buildsystems\msbuild\vcpkg.targets')) {
                return $bundled
            }
        }
    }
    return $null
}

if ($Clean) {
    $out = Join-Path $root 'out'
    if (Test-Path -LiteralPath $out) {
        Write-Host "build: removing $out"
        Remove-Item -LiteralPath $out -Recurse -Force
    }
}

$solution = Get-ChildItem -Path $root -Filter '*.slnx' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $solution) {
    $solution = Get-ChildItem -Path $root -Filter '*.sln' -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if (-not $solution) { throw 'build: no .slnx/.sln solution file found.' }

$msbuild = Get-MSBuild
$msbuildArgs = @(
    $solution.FullName
    '/nologo'
    '/m'
    "/p:Configuration=$Configuration"
    '/p:Platform=x64'
)
$vcpkgRoot = Get-VcpkgRoot
if ($vcpkgRoot) { $msbuildArgs += "/p:VcpkgRoot=$vcpkgRoot" }
if ($PlatformToolset) { $msbuildArgs += "/p:PlatformToolset=$PlatformToolset" }

Write-Host "build: $($solution.Name) $Configuration|x64"
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "build: failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

if ($Test) {
    $tests = Get-ChildItem -Path (Join-Path $root 'out') -Recurse -Include '*Tests*.exe', '*Test*.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\(obj|vcpkg_installed|\.git)\\' }
    if (-not $tests) { throw 'build: no test executable found.' }
    foreach ($testExe in $tests) {
        Write-Host "build: running $($testExe.FullName)"
        & $testExe.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Error "build: $($testExe.Name) failed with exit code $LASTEXITCODE."
            exit $LASTEXITCODE
        }
    }
}

Write-Host 'build: ok'
exit 0