; Pacecar Installer - Inno Setup Script
; Compile with: ISCC /DAppVersion=x.y.z installer\pacecar.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

#ifndef BuildDir
  #define BuildDir "..\target\release"
#endif

[Setup]
AppId={{B8F2C5A1-7D3E-4F9A-A2C6-1E5D8B4F7A3C}
AppName=Pacecar
AppVersion={#AppVersion}
AppVerName=Pacecar {#AppVersion}
AppPublisher=binbuf
AppPublisherURL=https://github.com/binbuf/pacecar
AppSupportURL=https://github.com/binbuf/pacecar/issues
DefaultDirName={autopf}\Pacecar
DefaultGroupName=Pacecar
LicenseFile=..\LICENSE
OutputDir=..\target\installer
OutputBaseFilename=pacecar-{#AppVersion}-setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\pacecar.exe
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startupentry"; Description: "Start Pacecar with Windows"; GroupDescription: "Other:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\pacecar.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\hwmon-shim.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\install-pawnio.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Pacecar"; Filename: "{app}\pacecar.exe"
Name: "{group}\Uninstall Pacecar"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Pacecar"; Filename: "{app}\pacecar.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Pacecar"; ValueData: """{app}\pacecar.exe"""; Flags: uninsdeletevalue; Tasks: startupentry

[Run]
Filename: "{app}\pacecar.exe"; Description: "{cm:LaunchProgram,Pacecar}"; Flags: nowait postinstall skipifsilent
