; Pacecar installer - Inno Setup script (native Win32 build).
;
; Compile with:
;   ISCC /DAppVersion=0.2.0 /DSourceDir=<abs path to the staged release dir> installer\pacecar.iss
;
; The staged source directory must contain everything the overlay needs at runtime:
; Pacecar.Overlay.exe, the optional elevated Pacecar.Sensors.exe helper and its native deps, and
; THIRD_PARTY_NOTICES.md. Debug symbols and test executables are excluded by the CI staging step.

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif

#ifndef SourceDir
  #define SourceDir "..\dist\Pacecar"
#endif

[Setup]
AppId={{630A84C3-6120-40CB-B695-F556A02F3BBC}
AppName=Pacecar
AppVersion={#AppVersion}
AppVerName=Pacecar {#AppVersion}
AppPublisher=binbuf
AppPublisherURL=https://github.com/binbuf/pacecar
AppSupportURL=https://github.com/binbuf/pacecar/issues
DefaultDirName={autopf}\Pacecar
DefaultGroupName=Pacecar
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=Pacecar-{#AppVersion}-setup
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\Pacecar.Overlay.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Pacecar"; Filename: "{app}\Pacecar.Overlay.exe"
Name: "{group}\Uninstall Pacecar"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Pacecar"; Filename: "{app}\Pacecar.Overlay.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Pacecar.Overlay.exe"; Description: "{cm:LaunchProgram,Pacecar}"; Flags: nowait postinstall skipifsilent