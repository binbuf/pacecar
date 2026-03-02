# install-pawnio.ps1 — Download and install the PawnIO kernel driver.
# PawnIO is a signed driver required by LibreHardwareMonitor >= 0.9.6 for
# hardware sensor access.  This script must be run as Administrator.

$ErrorActionPreference = 'Stop'

$version  = '1.2.0'
$url      = "https://github.com/AltimitSystems/PawnIO/releases/download/v$version/PawnIO_setup.exe"
$tempDir  = Join-Path $env:TEMP 'pacecar-pawnio'
$installer = Join-Path $tempDir 'PawnIO_setup.exe'

try {
    if (-not (Test-Path $tempDir)) {
        New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    }

    Write-Host "Downloading PawnIO v$version ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing

    Write-Host 'Running installer (requires admin) ...'
    $proc = Start-Process -FilePath $installer -ArgumentList '-install', '-silent' `
                          -Verb RunAs -Wait -PassThru

    if ($proc.ExitCode -ne 0) {
        Write-Warning "Installer exited with code $($proc.ExitCode)"
    } else {
        Write-Host 'PawnIO installed successfully.'
    }
}
catch {
    Write-Error "PawnIO installation failed: $_"
    exit 1
}
finally {
    # Clean up downloaded installer.
    if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
    }
}
