#requires -Version 7
# Independent verification for the symphony harness.
#
# Builds the native solution (Release|x64) with the repo's vcpkg manifest and runs the unit-test
# executable(s). Before the C++ solution is scaffolded (task T01) there is nothing to verify, so the
# script no-ops successfully. Exits non-zero on any build or test failure.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

if (-not (Test-Path -LiteralPath (Join-Path $root 'src'))) {
    Write-Host 'verify: src/ not present yet; nothing to build or test.'
    exit 0
}

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
    throw 'verify: MSBuild not found (install the VS C++ workload or put msbuild on PATH).'
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

$solution = Get-ChildItem -Path $root -Filter '*.slnx' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $solution) {
    $solution = Get-ChildItem -Path $root -Filter '*.sln' -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
if (-not $solution) {
    throw 'verify: no .slnx/.sln solution file found.'
}

$msbuild = Get-MSBuild
$msbuildArgs = @(
    $solution.FullName
    '/nologo'
    '/m'
    '/p:Configuration=Release'
    '/p:Platform=x64'
)
$vcpkgRoot = Get-VcpkgRoot
if ($vcpkgRoot) { $msbuildArgs += "/p:VcpkgRoot=$vcpkgRoot" }

Write-Host "verify: building $($solution.Name) (Release|x64)"
& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "verify: build failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

$tests = Get-ChildItem -Path $root -Recurse -Include '*Tests*.exe', '*Test*.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\(obj|vcpkg_installed|\.git)\\' }
if (-not $tests) {
    Write-Warning 'verify: build succeeded but no test executable was found.'
    exit 0
}

$alreadyRun = @{}
$failed = $false
foreach ($test in $tests) {
    if ($alreadyRun.ContainsKey($test.FullName)) { continue }
    $alreadyRun[$test.FullName] = $true
    Write-Host "verify: running $($test.FullName)"
    & $test.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "verify: $($test.Name) failed with exit code $LASTEXITCODE."
        $failed = $true
    }
}
if ($failed) { exit 1 }

Write-Host 'verify: ok'
exit 0
