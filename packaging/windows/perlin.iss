; Inno Setup script for the Perlin installer.
;
;   iscc /DAppVersion=1.0.123 /DSourceDir=..\..\dist /DOutputDir=..\..\pkg /DOutputName=Perlin-windows-x64-setup /DArch=x64 perlin.iss
;
; It installs for the current user by default (no administrator prompt), into
; %LOCALAPPDATA%\Programs\Perlin, so the built-in updater can replace the files in place. People
; who want it for every user can pick that on the first page.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\pkg"
#endif
#ifndef OutputName
  #define OutputName "Perlin-windows-x64-setup"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef AppUrl
  #define AppUrl "https://github.com/theperlinproject/perlin"
#endif

[Setup]
AppId={{6C0C2E57-4B1E-4C0B-9C5E-5E2D3F7A9B11}
AppName=Perlin
AppVersion={#AppVersion}
AppVerName=Perlin {#AppVersion}
AppPublisher=The Perlin Project
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
DefaultDirName={autopf}\Perlin
DefaultGroupName=Perlin
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile=perlin.ico
UninstallDisplayIcon={app}\Perlin.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything the build installed, except the portable marker (an installed copy keeps its data
; in %APPDATA%\Perlin like any other program).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "portable.txt"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Perlin"; Filename: "{app}\Perlin.exe"
Name: "{autodesktop}\Perlin"; Filename: "{app}\Perlin.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Perlin.exe"; Description: "{cm:LaunchProgram,Perlin}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Files the updater staged or left behind; the player's data in %APPDATA%\Perlin is kept.
Type: filesandordirs; Name: "{app}\.perlin-update"
Type: files; Name: "{app}\*.perlin-old"
