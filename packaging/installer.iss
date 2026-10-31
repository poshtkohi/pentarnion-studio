#define AppVersion "0.2"
#define AppName "Pentarnion Studio"
#ifndef Payload
  #define Payload AddBackslash(SourcePath) + "payload"
#endif

[Setup]
AppId={code:ApplicationIdentity}
UsePreviousLanguage=no
UsePreviousAppDir=no
UsePreviousGroup=no
UsePreviousSetupType=no
UsePreviousTasks=no
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Pentarnion Team
AppPublisherURL=https://pentarnion.com
AppSupportURL=https://pentarnion.com
DefaultDirName={localappdata}\Programs\Pentarnion Studio
DefaultGroupName=Pentarnion Studio
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir=release
OutputBaseFilename=Pentarnion Studio Installer
Compression=lzma2/normal
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=2
WizardStyle=modern
WizardSizePercent=125,125
DisableWelcomePage=no
DisableDirPage=no
DisableReadyPage=no
InfoBeforeFile=LICENSE-NOTICES.txt
SetupIconFile=assets\pentarnion-studio.ico
UninstallDisplayIcon={app}\pentarnion-studio.exe
UninstallDisplayName=Pentarnion Studio {#AppVersion}
CloseApplications=no
RestartApplications=no
AppMutex=PentarnionStudioManagedInstaller-v1
SetupLogging=yes
WizardImageFile=assets\wizard-wallpaper.bmp
WizardSmallImageFile=assets\wizard-small.bmp
VersionInfoVersion=0.2.0.0
VersionInfoDescription=Pentarnion Studio standalone Windows installer

[Messages]
WelcomeLabel1=Welcome to Pentarnion Studio
WelcomeLabel2=Explore algebras, numerical models and scientific visualisation.%n%nThis offline package installs for the current user without administrator rights. It includes Studio, the native GCC compiler, scientific libraries, graphics and documentation.%n%nYour writable examples and research files remain separate from the application.
InfoBeforeLabel=Component notices and preservation of your work
FinishedHeadingLabel=Pentarnion Studio is ready
FinishedLabel=Studio and its private native toolchain are installed.%n%nWritable examples are in Documents\Pentarnion Studio\Examples. Existing files were preserved.%n%nOpen the Quick Start for your first calculation.

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Dirs]
Name: "{code:WorkspaceDir}"; Flags: uninsneveruninstall
Name: "{code:WorkspaceDir}\Examples"; Flags: uninsneveruninstall

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak
Source: "{#Payload}\share\pentarnion-studio\examples\*"; DestDir: "{code:WorkspaceDir}\Examples"; Flags: onlyifdoesntexist uninsneveruninstall recursesubdirs createallsubdirs
Source: "incoming-file-hashes.txt"; Flags: dontcopy

[Icons]
Name: "{code:MenuDir}\Pentarnion Studio"; Filename: "{app}\pentarnion-studio.exe"; WorkingDir: "{code:WorkspaceDir}"
Name: "{code:MenuDir}\Examples"; Filename: "{code:WorkspaceDir}\Examples"
Name: "{code:MenuDir}\Documentation"; Filename: "{app}\documentation"
Name: "{code:MenuDir}\Quick Start"; Filename: "{app}\documentation\Pentarnion-Studio-Quick-Start.pdf"
Name: "{code:MenuDir}\Theory and Programming Language"; Filename: "{app}\documentation\Pentarnion-Theory-and-Programming-Language-Fundamentals.pdf"
Name: "{code:MenuDir}\Uninstall Pentarnion Studio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Pentarnion Studio"; Filename: "{app}\pentarnion-studio.exe"; WorkingDir: "{code:WorkspaceDir}"; Tasks: desktopicon

[Run]
Filename: "{app}\pentarnion-studio.exe"; WorkingDir: "{code:WorkspaceDir}"; Description: "Launch Pentarnion Studio"; Flags: nowait postinstall skipifsilent unchecked
Filename: "{app}\documentation\Pentarnion-Studio-Quick-Start.pdf"; Description: "Open the Quick Start"; Flags: shellexec postinstall skipifsilent unchecked

[Registry]
Root: HKCU; Subkey: "{code:StudioSettingsKey}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey

[Code]
function RuntimeGetFileAttributes(FileName: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function RuntimeCreateFile(FileName: String; Access, ShareMode: LongWord;
  Security: Integer; Creation, Attributes: LongWord; Template: Integer): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function RuntimeCloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function RuntimeFilePath(const FileName: String): String;
begin
  if Copy(FileName,1,4)='\\?\' then Result:=FileName
  else if Copy(FileName,1,2)='\\' then Result:='\\?\UNC\'+Copy(FileName,3,Length(FileName))
  else Result:='\\?\'+FileName;
end;

#include "uninstall_managed_application.iss"
#include "update_managed_application.iss"

var ContributorsPage: TWizardPage;

function TestTag: String;
var I: Integer;
begin
  Result:=ExpandConstant('{param:TESTID|}');
  if Length(Result)>60 then RaiseException('Invalid test identifier.');
  for I:=1 to Length(Result) do
    if Pos(Result[I],'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-')=0 then RaiseException('Invalid test identifier.');
end;

function ApplicationIdentity(Param: String): String;
begin
  Result:='D5143A9E-9981-4E20-A5D6-38DD0D19836E';
  if TestTag<>'' then Result:=Result+'-'+TestTag;
end;

function WorkspaceDir(Param: String): String;
begin
  Result:=ExpandConstant('{param:WORKSPACE|}');
  if Result='' then Result:=ExpandConstant('{userdocs}\Pentarnion Studio');
  Result:=ExpandFileName(Result);
end;

function MenuDir(Param: String): String;
begin
  Result:=ExpandConstant('{param:TESTMENUDIR|}');
  if Result='' then Result:=ExpandConstant('{userprograms}\Pentarnion Studio');
  Result:=ExpandFileName(Result);
end;

procedure Contributor(const Caption: String; Bold: Boolean; var Top: Integer);
var LabelControl: TNewStaticText;
begin
  LabelControl:=TNewStaticText.Create(WizardForm);
  LabelControl.Parent:=ContributorsPage.Surface;
  LabelControl.SetBounds(0,Top,ContributorsPage.SurfaceWidth,ScaleY(24));
  LabelControl.AutoSize:=False;LabelControl.WordWrap:=True;
  LabelControl.Font.Size:=9;
  if Bold then begin LabelControl.Font.Style:=[fsBold];LabelControl.Font.Color:=$744B16;end;
  LabelControl.Caption:=Caption;LabelControl.AdjustHeight;
  Top:=LabelControl.Top+LabelControl.Height+ScaleY(5);
end;

procedure InitializeWizard;
var Top: Integer;
begin
  ContributorsPage:=CreateCustomPage(wpWelcome,'Authors and contact','Pentarnion Studio');
  Top:=ScaleY(8);
  Contributor('Dr Alireza Poshtkohi',True,Top);
  Contributor('School of Physics, Engineering and Computer Science, University of Hertfordshire, Hatfield, Hertfordshire, United Kingdom',False,Top);
  Contributor('a.poshtkohi@herts.ac.uk (correspondence)',False,Top);
  Top:=Top+ScaleY(12);
  Contributor('Dr Susanne Pumplün',True,Top);
  Contributor('School of Mathematical Sciences, University of Nottingham, Nottingham NG7 2RD, United Kingdom',False,Top);
  Contributor('Susanne.Pumpluen@nottingham.ac.uk',False,Top);
  Top:=Top+ScaleY(12);Contributor('https://pentarnion.com',True,Top);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result:=True;
  if CurPageID=wpSelectDir then begin
    Result:=SafeApplicationDirectory(WizardDirValue);
    if not Result then SuppressibleMsgBox('Choose an empty application folder or an existing verified Pentarnion installation. Project and system folders are not valid installation targets.',mbError,MB_OK,IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result:='';
  try
    if not SafeApplicationDirectory(ExpandConstant('{app}')) then RaiseException('The application folder is not empty or is not owned by this installer.');
    if OwnedApplicationDirectory(ExpandConstant('{app}')) and not AllOwnedShortcutPathsSafe(ExpandConstant('{app}')) then RaiseException('The earlier installation shortcut ownership cannot be verified. Restore linked shortcut paths, or uninstall the earlier installation before updating. External workspaces will be preserved.');
    if SameOrParentPath(ExpandConstant('{app}'),WorkspaceDir('')) or SameOrParentPath(WorkspaceDir(''),ExpandConstant('{app}')) then RaiseException('Keep the workspace separate from the application folder.');
    AssertTreeHasNoLinks(WorkspaceDir(''));AssertTreeHasNoLinks(MenuDir(''));
    BackupReplacedApplicationFiles;
  except Result:=GetExceptionMessage;end;
end;

procedure RecordOwnedShortcutPaths;
var Paths,Names: TStringList;Bytes: AnsiString;Path,FileName: String;I,J: Integer;Present: Boolean;
begin
  FileName:=ExpandConstant('{app}\installer-shortcut-paths.txt');Paths:=TStringList.Create;Names:=TStringList.Create;
  try
    if LoadStringFromFile(RuntimeFilePath(FileName),Bytes) then Paths.Text:=UTF8Decode(Bytes);
    if Paths.Count=0 then Paths.Add('PentarnionShortcutPaths-v1');
    if Paths[0]<>'PentarnionShortcutPaths-v1' then RaiseException('Invalid earlier shortcut ownership record.');
    Names.Add(AddBackslash(MenuDir(''))+'Pentarnion Studio.lnk');
    Names.Add(AddBackslash(MenuDir(''))+'Examples.lnk');Names.Add(AddBackslash(MenuDir(''))+'Documentation.lnk');
    Names.Add(AddBackslash(MenuDir(''))+'Quick Start.lnk');Names.Add(AddBackslash(MenuDir(''))+'Theory and Programming Language.lnk');
    Names.Add(AddBackslash(MenuDir(''))+'Uninstall Pentarnion Studio.lnk');
    if WizardIsTaskSelected('desktopicon') then Names.Add(ExpandConstant('{autodesktop}\Pentarnion Studio.lnk'));
    // Keep EVERY earlier owned path. Inno retains logged shortcut removals even
    // when a subsequent update changes its menu location or desktop task.
    for I:=0 to Names.Count-1 do begin
      Path:=ExpandFileName(Names[I]);Present:=False;
      for J:=1 to Paths.Count-1 do if CompareText(Paths[J],Path)=0 then Present:=True;
      if not Present then Paths.Add(Path);
    end;
    if not SaveStringToFile(RuntimeFilePath(FileName),UTF8Encode(Paths.Text),False) then RaiseException('Cannot preserve shortcut ownership history.');
  finally Names.Free;Paths.Free;end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var DesktopPath: String;
begin
  if CurStep=ssPostInstall then begin
    WriteInstallRootOwner;
    if not SaveStringToFile(ExpandConstant('{app}\workspace-path.txt'),UTF8Encode(WorkspaceDir('')+#13#10),False) then RaiseException('Cannot record the writable workspace.');
    if not SaveStringToFile(ExpandConstant('{app}\installer-menu-path.txt'),UTF8Encode(MenuDir('')+#13#10),False) then RaiseException('Cannot record installation identity.');
    DesktopPath:='none';
    if WizardIsTaskSelected('desktopicon') then DesktopPath:=ExpandConstant('{autodesktop}\Pentarnion Studio.lnk');
    if not SaveStringToFile(ExpandConstant('{app}\installer-desktop-path.txt'),UTF8Encode(DesktopPath+#13#10),False) then RaiseException('Cannot record desktop shortcut ownership.');
    RecordOwnedShortcutPaths;
  end;
end;
