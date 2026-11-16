; OSDS (Open Source Driving Simulator) Windows installer (Inno Setup 6).
; OSDS was named SimCruise until November 2026: the AppId, the program file
; (SimCruise.exe), the package folder and %LOCALAPPDATA%\SimCruise keep the old
; name so that an update installs over 2.0.3 and older and keeps the player's settings.
;
; Build with scripts\installer.ps1, which packages nothing itself: it reads the
; Shipping package in Saved\Package\Windows (scripts\package.ps1 -Configuration
; Shipping) and passes the version and the folders to ISCC:
;
;   ISCC.exe /DAppVersion=2.0.0 /DSourceDir=<package>\Windows /DOutputDir=<out> packaging\windows\simcruise.iss
;
; The installer asks "for me only" (no administrator rights, under
; %LOCALAPPDATA%\Programs) or "for all users" (Program Files). It adds a Start
; menu entry and, if chosen, a desktop icon, and registers an uninstaller.
;
; A clean Windows 10 or 11 does not have two parts the game needs (scripts\dep-check.py):
;   - the Microsoft Visual C++ 2015-2022 x64 runtime (MSVCP140, MSVCP140_1, VCRUNTIME140,
;     VCRUNTIME140_1). Without it SimCruise.exe cannot start.
;   - Microsoft GameInput (racing wheels; the launcher asks for it at the start).
; The package carries Microsoft's signed installers for both in Engine\Extras\Redist\en-us
; (scripts\package.ps1). After the files are copied, this installer runs each one that the PC
; needs. They install for the whole PC, so Windows asks for permission (UAC) when the game
; installs "for me only". If the player says no, the installer says what is missing, and the
; launcher SimCruise.exe asks again at the start.
;
; The uninstaller deletes the map cache, downloaded photos, other caches and logs in
; %LOCALAPPDATA%\SimCruise. It asks before it deletes the player's settings, tuning and
; screenshots; a silent uninstall keeps them.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\Saved\Package\Windows"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\Saved\Package"
#endif
; The version of the packaged vc_redist.x64.exe (scripts\installer.ps1 reads it): an older
; runtime on the PC is replaced, because a program built with a newer toolset can crash on it.
; Tests build with another AppId, so a test install never replaces a real one on the PC.
#ifndef AppIdGuid
  #define AppIdGuid "6E2B8B53-4C0F-4D63-9A4B-5C1D2F0E7A91"
#endif
#ifndef VcMajor
  #define VcMajor "14"
#endif
#ifndef VcMinor
  #define VcMinor "50"
#endif
#ifndef VcBuild
  #define VcBuild "35719"
#endif

[Setup]
AppId={{{#AppIdGuid}}
AppName=OSDS
AppVersion={#AppVersion}
AppVerName=OSDS {#AppVersion}
AppPublisher=OSDS contributors
AppPublisherURL=https://github.com/simcruisegame/simcruise
AppSupportURL=https://github.com/simcruisegame/simcruise/issues
AppUpdatesURL=https://github.com/simcruisegame/simcruise/releases
DefaultDirName={autopf}\OSDS
DefaultGroupName=OSDS
; An update from 2.0.3 or older moves the Start menu entry from the old SimCruise folder to OSDS.
UsePreviousGroup=no
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE.txt
; What the player must know first: internet, the Microsoft runtimes, the PC it needs.
InfoBeforeFile=before-you-install.txt
OutputDir={#OutputDir}
OutputBaseFilename=OSDS-{#AppVersion}-windows-x64-setup
SetupIconFile=osds.ico
UninstallDisplayIcon={app}\SimCruise.exe
UninstallDisplayName=OSDS {#AppVersion}
; Per-user install by default; the first page offers "all users" (needs administrator rights).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4
DiskSpanning=no
WizardStyle=modern
CloseApplications=yes
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=OSDS

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabel=Setup has installed [name] on your computer. Start it from the Start menu.%n%nOcean Drive, Miami Beach works with no internet. All other places need an internet connection.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Every .onnx from an earlier install goes before the files are copied: 2.0.0 shipped weights that
; may not ship now, and from 2.0.3 the package brings exactly the cleared ones (scripts\ShippedModels.ps1).
Type: files; Name: "{app}\SimCruise\data\ai\*.onnx"
; The shortcuts up to 2.0.3 had the old name, SimCruise.
Type: filesandordirs; Name: "{autoprograms}\SimCruise"
Type: files; Name: "{autodesktop}\SimCruise.lnk"

[Files]
; The whole Shipping package except debug symbols, the build manifests and any local test run's Saved folder.
; It includes Engine\Extras\Redist\en-us (the Microsoft runtime installers), which the launcher uses too.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,Manifest_*.txt,\SimCruise\Saved\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\OSDS"; Filename: "{app}\SimCruise.exe"; WorkingDir: "{app}"
Name: "{group}\OSDS controls"; Filename: "{app}\CONTROLS.md"
Name: "{group}\{cm:UninstallProgram,OSDS}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OSDS"; Filename: "{app}\SimCruise.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Only when the runtimes are installed: without them the game cannot start.
Filename: "{app}\SimCruise.exe"; Description: "{cm:LaunchProgram,OSDS}"; Flags: nowait postinstall skipifsilent; Check: RuntimesReady

[UninstallDelete]
; Files the game writes next to itself (none in a normal Shipping run; a crash may leave a folder).
Type: filesandordirs; Name: "{app}\SimCruise\Saved"

[Code]
const
  VcKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  RedistDir = '{app}\Engine\Extras\Redist\en-us';
  VcHelpUrl = 'https://aka.ms/vs/17/release/vc_redist.x64.exe';
  ErrorCancelled = 1223;

var
  MissingParts: String;

// The packed file version a.b.c.d of the runtime this package needs.
function VcNeededMS(): Cardinal;
begin
  Result := ({#VcMajor} shl 16) or {#VcMinor};
end;

function VcNeededLS(): Cardinal;
begin
  Result := {#VcBuild} shl 16;
end;

function FileAtLeast(const Path: String; NeedMS, NeedLS: Cardinal): Boolean;
var
  MS, LS: Cardinal;
begin
  Result := FileExists(Path) and GetVersionNumbers(Path, MS, LS) and
    ((MS > NeedMS) or ((MS = NeedMS) and (LS >= NeedLS)));
end;

// True when Windows has the Visual C++ 2015-2022 x64 runtime at the packaged version or newer:
// the four DLLs the game loads (in the 64-bit System32) and the runtime's registry entry, which
// the launcher SimCruise.exe reads.
function VcRuntimeOk(): Boolean;
var
  Installed: Cardinal;
  Sys: String;
begin
  Sys := ExpandConstant('{sys}');
  Result := FileAtLeast(Sys + '\msvcp140.dll', VcNeededMS(), VcNeededLS()) and
    FileAtLeast(Sys + '\vcruntime140.dll', VcNeededMS(), VcNeededLS()) and
    FileExists(Sys + '\msvcp140_1.dll') and FileExists(Sys + '\vcruntime140_1.dll');
  if Result then
  begin
    Installed := 0;
    if not RegQueryDWordValue(HKLM64, VcKey, 'Installed', Installed) then
      RegQueryDWordValue(HKLM32, VcKey, 'Installed', Installed);
    Result := Installed = 1;
  end;
end;

// GameInput: the library the launcher and the wheel input look for.
function GameInputOk(): Boolean;
begin
  Result := FileExists(ExpandConstant('{sys}\GameInputRedist.dll'));
end;

function RuntimesReady(): Boolean;
begin
  Result := VcRuntimeOk();
end;

// Runs a Microsoft installer for the whole PC. In a "for me only" install, Windows asks for
// permission; True when it ran and returned success (0, 3010 restart needed, 1638 newer present).
function RunForPC(const FileName, Params, What: String): Boolean;
var
  Code: Integer;
  Started: Boolean;
begin
  WizardForm.StatusLabel.Caption := 'Installing ' + What + '. Windows can ask for your permission.';
  WizardForm.Update;
  if IsAdminInstallMode then
    Started := Exec(FileName, Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code)
  else
    Started := ShellExec('runas', FileName, Params, '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code);
  if not Started then
  begin
    if Code = ErrorCancelled then
      Log(What + ': the permission was not given')
    else
      Log(What + ': could not start (' + SysErrorMessage(Code) + ')');
    Result := False;
    Exit;
  end;
  Log(What + ': exit code ' + IntToStr(Code));
  Result := (Code = 0) or (Code = 3010) or (Code = 1641) or (Code = 1638);
end;

procedure InstallRuntimes();
var
  Redist: String;
begin
  Redist := ExpandConstant(RedistDir);
  MissingParts := '';
  if VcRuntimeOk() then
    Log('Visual C++ runtime: already installed')
  else
  begin
    RunForPC(Redist + '\vc_redist.x64.exe', '/install /quiet /norestart', 'the Microsoft Visual C++ runtime');
    if not VcRuntimeOk() then
      MissingParts := MissingParts + '- Microsoft Visual C++ 2015-2022 runtime (x64). Without it, the game cannot start.' + #13#10;
  end;
  if GameInputOk() then
    Log('GameInput: already installed')
  else
  begin
    RunForPC(ExpandConstant('{sys}\msiexec.exe'), '/i "' + Redist + '\GameInputRedist.msi" /quiet /norestart', 'Microsoft GameInput');
    if not GameInputOk() then
      MissingParts := MissingParts + '- Microsoft GameInput. Without it, racing wheels do not work, and the game asks for it when it starts.' + #13#10;
  end;
  if MissingParts <> '' then
  begin
    Log('Not installed: ' + MissingParts);
    SuppressibleMsgBox('These Microsoft parts are not installed:' + #13#10#13#10 + MissingParts + #13#10 +
      'To install them, do one of these steps:' + #13#10 +
      '1. Start OSDS. It asks to install them. Click Yes, and then give Windows the permission.' + #13#10 +
      '2. Or run the files in this folder as administrator:' + #13#10 + Redist + #13#10 +
      '3. Or get the Visual C++ runtime from Microsoft: ' + VcHelpUrl,
      mbError, MB_OK, IDOK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallRuntimes();
end;

// Uninstall: the caches and logs go; the player's own files stay unless the player says yes.
function IsPlayerData(const Name: String): Boolean;
var
  Low: String;
begin
  Low := Lowercase(Name);
  // SimCruise: preferences.json and tuning-*.json. Config: graphics and input settings.
  Result := (Low = 'simcruise') or (Low = 'config') or (Low = 'screenshots') or (Low = 'savegames');
end;

procedure DeleteCaches(const Base: String);
var
  Find: TFindRec;
  Saved: String;
begin
  // The map helper's cache (map data, terrain, aerial photos; the helper keeps it near 100 MB).
  DelTree(Base + '\map-cache', True, True, True);
  Saved := Base + '\Saved';
  if FindFirst(Saved + '\*', Find) then
  try
    repeat
      if (Find.Name <> '.') and (Find.Name <> '..') then
      begin
        if (Find.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
        begin
          // Live (downloaded photos and weather), NeuralDetail, Knowledge, Scan, Logs, Crashes ...
          if not IsPlayerData(Find.Name) then
            DelTree(Saved + '\' + Find.Name, True, True, True);
        end
        else
          DeleteFile(Saved + '\' + Find.Name);
      end;
    until not FindNext(Find);
  finally
    FindClose(Find);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Base: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // /SimCruiseDataDir=<folder> on the uninstaller's command line: the uninstall test uses a copy.
  Base := ExpandConstant('{param:SimCruiseDataDir|{localappdata}\SimCruise}');
  if not DirExists(Base) then
    Exit;
  DeleteCaches(Base);
  if (not UninstallSilent) and
    (MsgBox('Do you also want to delete your OSDS settings, car tuning and screenshots?' + #13#10#13#10 +
      'They are in ' + Base + '.' + #13#10 + 'Click No to keep them for a new install.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    DelTree(Base, True, True, True)
  else
  begin
    // Remove the folders that are now empty.
    RemoveDir(Base + '\Saved');
    RemoveDir(Base);
  end;
end;
