; AetherSDR for OpenHPSDR by N7DDC -- Windows installer (Inno Setup 6)
;
; ANAN fork build 729 (operator, 25.11: "an installer for Windows -- licence,
; user agreement, choice of the install folder, installation, registration of
; the program in the OS, a desktop shortcut; installing again replaces the
; previous version except the settings"). Made from AetherSDR's own
; packaging\windows\installer.iss, kept unchanged beside it.
;
; Build (Inno Setup 6, https://jrsoftware.org/isinfo.php):
;   packaging\windows\build-installer-anan.cmd
; or by hand:
;   ISCC /DAPP_VERSION=729 /DDEPLOY_DIR="C:\Users\...\AetherSDR-Release" installer-anan.iss
;
; DEPLOY_DIR is the folder the program runs from (AetherSDR.exe, its DLLs and
; the Qt plugin folders) -- the release folder; *.zip in it are left out.
;
; The settings are not in the program folder (%LOCALAPPDATA%\AetherSDR_ugly_fork,
; %USERPROFILE%\AetherSDR-FTX): neither an update nor an uninstall touches them.

#ifndef APP_VERSION
  #define APP_VERSION "0"
#endif
#ifndef DEPLOY_DIR
  #define DEPLOY_DIR "..\..\deploy"
#endif
#define APP_NAME "AetherSDR for OpenHPSDR by N7DDC"
#define APP_EXE "AetherSDR.exe"

[Setup]
; Its own identity, so it lives beside AetherSDR and neither replaces the other.
AppId={{6D700F17-1817-46CE-B832-A3CD64ECFCC7}
AppName={#APP_NAME}
AppVersion=build {#APP_VERSION}
AppVerName={#APP_NAME} build {#APP_VERSION}
AppPublisher=N7DDC
AppPublisherURL=https://github.com/Dfinitski/Ater-ugly_fork
AppSupportURL=https://github.com/Dfinitski/Ater-ugly_fork/issues
AppUpdatesURL=https://github.com/Dfinitski/Ater-ugly_fork/releases
AppCopyright=Copyright (C) AetherSDR contributors; ANAN / openHPSDR fork N7DDC
VersionInfoVersion=1.0.0.{#APP_VERSION}
VersionInfoProductName={#APP_NAME}
; The user agreement is the page that must be accepted; the GPL is shown
; before it.
LicenseFile=anan-user-agreement.txt
InfoBeforeFile=..\..\LICENSE
DefaultDirName={autopf}\{#APP_NAME}
DefaultGroupName={#APP_NAME}
DisableDirPage=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#APP_EXE}
UninstallDisplayName={#APP_NAME}
SetupIconFile=AetherSDR.ico
OutputBaseFilename=AetherSDR-OpenHPSDR-N7DDC-build{#APP_VERSION}-win64-setup
OutputDir=..\..
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
WizardImageFile=wizard-image.bmp
WizardSmallImageFile=wizard-small-image.bmp
DisableWelcomePage=no
; As AetherSDR: no administrator needed (a per-user install), or Program Files
; for all users when the installer is allowed to elevate.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
FinishedLabel=Setup has finished installing [name] on your computer. The application may be launched by selecting the installed shortcuts.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; Installing again replaces the previous version: everything in the folder
; that previous install made goes first, so no file of an older build stays
; behind. Only when this folder IS that install (IsOurPreviousFolder): a
; folder chosen for the first time is never emptied. The settings are not
; here and stay.
Type: filesandordirs; Name: "{app}\*"; Check: IsOurPreviousFolder

[Files]
Source: "{#DEPLOY_DIR}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.zip,*.log,*.pdb"

[Icons]
Name: "{group}\{#APP_NAME}"; Filename: "{app}\{#APP_EXE}"
Name: "{group}\Uninstall {#APP_NAME}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#APP_NAME}"; Filename: "{app}\{#APP_EXE}"; Tasks: desktopicon

[Run]
; Windows Firewall, as AetherSDR's installer does it (one elevated step):
; radio discovery and streams are UDP on every network, the TCP servers
; (TCI, CAT) on domain and private networks only. Rule names of their own.
Filename: "{cmd}"; Parameters: "{code:FirewallParams}"; WorkingDir: "{sys}"; Verb: "runas"; StatusMsg: "Configuring Windows Firewall..."; Flags: shellexec runhidden waituntilterminated
Filename: "{app}\{#APP_EXE}"; Description: "Launch {#APP_NAME}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/D /C ""netsh advfirewall firewall delete rule name=""{#APP_NAME} (TCP-In)"" & netsh advfirewall firewall delete rule name=""{#APP_NAME} (UDP-In)"" & netsh advfirewall firewall delete rule name=""{#APP_NAME} D-STAR Waveform RX"""""; WorkingDir: "{sys}"; Verb: "runas"; Flags: shellexec runhidden waituntilterminated; RunOnceId: "RemoveFirewallRules"

[Code]
// The folder of the install this one replaces, from Inno's own uninstall
// record for this AppId (HKLM for an all-users install, HKCU per user).
function PreviousInstallDir(): String;
var
  Key, Dir: String;
begin
  Result := '';
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6D700F17-1817-46CE-B832-A3CD64ECFCC7}_is1';
  if RegQueryStringValue(HKLM, Key, 'Inno Setup: App Path', Dir) or
     RegQueryStringValue(HKCU, Key, 'Inno Setup: App Path', Dir) then
    Result := Dir;
end;

// True only when the folder being installed to is the previous install's
// and it holds the program -- the only case where emptying it is safe.
function IsOurPreviousFolder(): Boolean;
var
  Prev, App: String;
begin
  Prev := RemoveBackslashUnlessRoot(PreviousInstallDir());
  App := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  Result := (Prev <> '') and (CompareText(Prev, App) = 0)
            and FileExists(AddBackslash(App) + '{#APP_EXE}');
end;

function FirewallParams(Param: String): String;
var
  App, Netsh, Cmd: String;
begin
  App := ExpandConstant('{app}');
  Netsh := '"' + ExpandConstant('{sys}') + '\netsh.exe" advfirewall firewall ';
  Cmd := Netsh + 'delete rule name=all dir=in program="' + App + '\{#APP_EXE}" >NUL 2>&1'
    + ' & ' + Netsh + 'add rule name="{#APP_NAME} (UDP-In)" dir=in action=allow program="' + App + '\{#APP_EXE}" enable=yes profile=any protocol=UDP'
    + ' && ' + Netsh + 'add rule name="{#APP_NAME} (TCP-In)" dir=in action=allow program="' + App + '\{#APP_EXE}" enable=yes profile=domain,private protocol=TCP';
  if FileExists(App + '\aether-dv-waveform.exe') then
    Cmd := Netsh + 'delete rule name="{#APP_NAME} D-STAR Waveform RX" >NUL 2>&1'
      + ' & ' + Netsh + 'add rule name="{#APP_NAME} D-STAR Waveform RX" dir=in action=allow program="' + App + '\aether-dv-waveform.exe" enable=yes profile=any protocol=UDP'
      + ' & ' + Cmd;
  Result := '/D /C "' + Cmd + '"';
end;
