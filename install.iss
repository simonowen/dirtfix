; Inno Setup script for DirtFix installer

#define MyAppName "DirtFix"
#define MyAppExeName MyAppName + ".exe"
#define VerMajor
#define VerMinor
#define VerRev
#define VerBuild
#define FullVersion=ParseVersion("Release\" + MyAppExeName, VerMajor, VerMinor, VerRev, VerBuild)
#define MyAppVersion Str(VerMajor) + "." + Str(VerMinor)
#define MyAppPublisher "Simon Owen"
#define MyAppURL "https://github.com/simonowen/dirtfix"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{E693D1E7-CF41-48CE-8E10-0A7FB6D6AE03}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayName={#MyAppName}
DisableProgramGroupPage=auto
OutputDir=.
OutputBaseFilename={#MyAppName}-{#StringChange(MyAppVersion, '.', '')}
VersionInfoVersion={#FullVersion}
Compression=lzma
SolidCompression=yes
SignTool=signtool $f
SignedUninstaller=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupAppTitle=Setup {#MyAppName}
SetupWindowTitle=Setup - {#MyAppName} v{#MyAppVersion}

[Files]
Source: "Release\DirtFix.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "Release\dinput8_32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Release\dinput8_64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "ReadMe.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "License.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{commonprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{commonprograms}\{#MyAppName} Website"; Filename: "https://github.com/simonowen/dirtfix"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "{#MyAppName}"; ValueType: none; Flags: deletevalue
Root: HKCU; Subkey: "Software\SimonOwen\{#MyAppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\SimonOwen"; Flags: uninsdeletekeyifempty

[InstallDelete]
Type: files; Name: "{app}\inject.dll"

[Run]
Filename: "{app}\{#MyAppName}"; Flags: postinstall

[UninstallRun]
Filename: "{app}\{#MyAppName}"; Parameters: "/uninstall"

[Code]

function GetAppPid(const ExeName : string): Integer;
var
    WbemLocator: Variant;
    WbemServices: Variant;
    WbemObjectSet: Variant;
begin
    WbemLocator := CreateOleObject('WBEMScripting.SWBEMLocator');
    WbemServices := WbemLocator.ConnectServer('.', 'root\CIMV2');
    WbemObjectSet := WbemServices.ExecQuery(Format('SELECT ProcessId FROM Win32_Process Where Name="%s"',[ExeName]));
    if WbemObjectSet.Count > 0 then
      Result := WbemObjectSet.ItemIndex(0).ProcessId
    else
      Result := 0;
end;

procedure CloseApplication(const ExeName: String);
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('taskkill.exe'), '/f /im ' + '"' + ExeName + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := ''

  if GetAppPid('DirtFix.exe') <> 0 then
  begin
      CloseApplication('{#MyAppExeName}')
  end;
end;
