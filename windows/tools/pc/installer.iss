; FLX Film Lab's installer for Windows (Inno Setup 7). tools\pc\package.cmd
; builds it from dist\FLX Film Lab once the app is installed there:
;   ISCC /DAppVersion=2.1.0 tools\pc\installer.iss
;
; It installs for the person running it, with no administrator rights, in
; their own Programs folder, or for everyone if they choose that in its
; first dialog. It puts the app in the Start menu, and in Explorer's
; "Open with" for RAW files, JPEG, TIFF and movies, without taking any of
; them over: what a file opens with by default stays as it was. Uninstalling
; takes all of that away again; the app's settings stay.

#ifndef AppVersion
  #define AppVersion "2.1.0"
#endif
#define AppName "FLX Film Lab DNG"
#define AppExe "FLXFilmLab.exe"

[Setup]
AppId={{06D859FF-5D9A-4C96-9D69-5FC91C8997FA}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=FLX Film Lab DNG fork
AppCopyright=FLX Film Lab contributors. GPL-3.0.
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809, the oldest Qt 6 runs on
MinVersion=10.0.17763
SourceDir=..\..
OutputDir=dist
OutputBaseFilename={#AppName} {#AppVersion} Setup
SetupIconFile=app\flx.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=no
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "dist\FLX Film Lab\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
