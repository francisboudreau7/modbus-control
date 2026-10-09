; Inno Setup 6 script for Modbus Control.
;
; Normally compiled by installer\build_installer.ps1, which passes the defines below.
; Manual compile (all paths absolute):
;   ISCC.exe /DAppVersion=1.0.0 /DSourceDir=<repo>\out\install /DRepoDir=<repo> ^
;            /O<repo>\out\installer installer\modbus_control.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\out\install"
#endif
#ifndef RepoDir
  #define RepoDir ".."
#endif
#ifndef OutputDir
  #define OutputDir "..\out\installer"
#endif

#define MyAppName "Modbus Control"

[Setup]
; Fixed identifier: keep it the same in every release so upgrades replace the old version.
AppId={{39296A81-3C82-44F9-B595-3D52E9171D44}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
DefaultDirName={autopf}\Modbus Control
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=ModbusControl-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no UAC prompt). The dialog (and the /CURRENTUSER or /ALLUSERS
; switches) allow an all-users install into Program Files.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
UninstallDisplayName={#MyAppName} {#AppVersion}
UninstallDisplayIcon={app}\bin\modbus_control.exe
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Everything CMake installed (bin\, share\, licenses\).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Sample project, documentation and readme from the repository.
Source: "{#RepoDir}\examples\*.mbproj"; DestDir: "{app}\examples"; Flags: ignoreversion
Source: "{#RepoDir}\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RepoDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\modbus_control.exe"; WorkingDir: "{app}\bin"
Name: "{group}\Modbus Test Server"; Filename: "{app}\bin\modbus_test_server.exe"; Parameters: "--host 127.0.0.1 --port 1502"; WorkingDir: "{app}\bin"
Name: "{group}\User Guide"; Filename: "{app}\docs\USER_GUIDE.md"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\modbus_control.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Registry]
; .mbproj file association. HKA maps to HKCU for a per-user install and to HKLM for an all-users install.
Root: HKA; Subkey: "Software\Classes\.mbproj"; ValueType: string; ValueName: ""; ValueData: "ModbusControl.Project"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\ModbusControl.Project"; ValueType: string; ValueName: ""; ValueData: "Modbus Control project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\ModbusControl.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\modbus_control.exe,0"
Root: HKA; Subkey: "Software\Classes\ModbusControl.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\modbus_control.exe"" ""%1"""

[Run]
Filename: "{app}\bin\modbus_control.exe"; Description: "Launch {#MyAppName}"; WorkingDir: "{app}\bin"; Flags: nowait postinstall skipifsilent
