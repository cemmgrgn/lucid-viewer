; Lucid Viewer installer (Inno Setup 6.3 or later)
;
; Build:   iscc installer\LucidViewer.iss
; Options: /DSourceDir=<folder with the release build>  /DAppExe=<exe name>  /DAppVersion=1.2.3
;
; Silent install examples:
;   LucidViewer-Setup-x64.exe /SILENT
;   LucidViewer-Setup-x64.exe /VERYSILENT /COMPONENTS="main,models\bgremove" /TASKS="desktopicon,convertmenu,fileassoc"

#define AppName        "Lucid Viewer"
#define AppId          "LucidViewer"
#define AppPublisher   "Cem Girgin"
#define AppURL         "https://github.com/cemmgrgn/lucid-viewer"

#ifndef SourceDir
  #define SourceDir    "..\src\LucidViewer\bin\x64\Release"
#endif
#ifndef AppExe
  #define AppExe       "LucidViewer.exe"
#endif
; Folder name the program uses under %APPDATA% and %LOCALAPPDATA%
#ifndef AppDataName
  #define AppDataName  "LucidViewer"
#endif
; Mutexes the running program holds (main window and screenshot tray)
#ifndef AppMutexes
  #define AppMutexes   "LucidViewer.Instance,LucidViewerScreenshotTray"
#endif
; Name of the HKCU\...\Run value the tray tool writes for "Start with Windows"
#ifndef AutostartValue
  #define AutostartValue "Lucid Viewer Screenshot"
#endif
#ifndef AppVersion
  ; major.minor.patch from the version resource of the program
  #define VerMajor
  #define VerMinor
  #define VerPatch
  #define VerBuild
  #expr ParseVersion(AddBackslash(SourceDir) + AppExe, VerMajor, VerMinor, VerPatch, VerBuild)
  #define AppVersion   Str(VerMajor) + "." + Str(VerMinor) + "." + Str(VerPatch)
#endif

#define ProgId         "LucidViewer.Image"
; Settings file next to the program (defaults for all users) and its section
#ifndef IniName
  #define IniName      "LucidViewer.ini"
#endif
#ifndef IniSection
  #define IniSection   "LucidViewer"
#endif

; AI models downloaded during setup
#define BgModelUrl     "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx"
#define BgModelFile    "u2net.onnx"
#define BgModelSha256  "8d10d2f3bb75ae3b6d527c77944fc5e7dcd94b29809d47a739a7a728a912b491"
#define BgModelSizeMB  "168"

[Setup]
AppId={{6A3E9F52-5C1B-4E7A-9C1D-7B2F4A8E0C31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
#ifndef NoLicensePage
LicenseFile=..\LICENSE.txt
#endif
OutputDir=Output
OutputBaseFilename=LucidViewer-{#AppVersion}-Setup-x64
SetupIconFile=..\docs\brand\LucidViewer.ico
WizardStyle=modern
WizardImageFile=res\wizard-large-100.bmp,res\wizard-large-125.bmp,res\wizard-large-150.bmp,res\wizard-large-200.bmp
WizardSmallImageFile=res\wizard-small-100.bmp,res\wizard-small-125.bmp,res\wizard-small-150.bmp,res\wizard-small-200.bmp
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809: needed by Windows ML (background removal) and the C++/WinRT OCR code
MinVersion=10.0.17763
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
AppMutex={#AppMutexes}
CloseApplications=yes
ChangesAssociations=yes
UsedUserAreasWarning=no
; same product name and 4-part version as the program (SignPath checks them, .signpath/artifact-configurations)
VersionInfoVersion={#AppVersion}.0
VersionInfoTextVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}.0
VersionInfoProductTextVersion={#AppVersion}.0
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright=Copyright 2026 {#AppPublisher}. GPL v2
VersionInfoDescription={#AppName} Setup

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"

[CustomMessages]
en.CompMain=Lucid Viewer (required)
tr.CompMain=Lucid Viewer (gerekli)
en.CompModels=Optional AI models (downloaded during setup)
tr.CompModels=İsteğe bağlı yapay zekâ modelleri (kurulum sırasında indirilir)
en.CompBgRemove=Background removal model, U²-Net ({#BgModelSizeMB} MB download)
tr.CompBgRemove=Arka plan kaldırma modeli, U²-Net ({#BgModelSizeMB} MB indirme)
en.TypeStandard=Standard installation
tr.TypeStandard=Standart kurulum
en.TypeFull=Full installation (with AI models)
tr.TypeFull=Tam kurulum (yapay zekâ modelleriyle)
en.TypeCustom=Custom installation
tr.TypeCustom=Özel kurulum
en.GroupShortcuts=Shortcuts:
tr.GroupShortcuts=Kısayollar:
en.GroupExplorer=Windows integration:
tr.GroupExplorer=Windows ile tümleştirme:
en.TaskDesktop=Create a &desktop shortcut
tr.TaskDesktop=&Masaüstü kısayolu oluştur
en.TaskStartMenu=Create a &Start menu shortcut
tr.TaskStartMenu=&Başlat menüsü kısayolu oluştur
en.TaskConvertMenu=Add "Convert to" to the Explorer &right-click menu for images
tr.TaskConvertMenu=Görüntülerin Explorer &sağ tık menüsüne "Dönüştür" ekle
en.TaskFileAssoc=Register Lucid Viewer as a photo viewer (shown in Open with and Default apps)
tr.TaskFileAssoc=Lucid Viewer'ı fotoğraf görüntüleyici olarak kaydet (Birlikte aç ve Varsayılan uygulamalar'da görünür)
en.PrefTitle=Preferences
tr.PrefTitle=Tercihler
en.PrefDesc=Choose how Lucid Viewer should work.
tr.PrefDesc=Lucid Viewer'ın nasıl çalışacağını seçin.
en.PrefSub=You can change all of these later in Tools > Settings.
tr.PrefSub=Bunların hepsini daha sonra Araçlar > Ayarlar'dan değiştirebilirsiniz.
en.PrefStartScreen=Show the start screen with recent and pinned photos when no image is open
tr.PrefStartScreen=Görüntü açık değilken son açılan ve sabitlenen fotoğrafları gösteren giriş ekranını göster
en.PrefRecent=Remember recently opened photos
tr.PrefRecent=Son açılan fotoğrafları hatırla
en.PrefScreenshot=Screenshot tool on the Print Screen key (starts with Windows, in the notification area)
tr.PrefScreenshot=Print Screen tuşunda ekran görüntüsü aracı (Windows ile başlar, bildirim alanında)
en.PrefShotHistory=Keep every screenshot, also the copied ones, and show them on the start screen
tr.PrefShotHistory=Kopyalananlar dahil her ekran görüntüsünü sakla ve giriş ekranında göster
en.PrefWheel=Mouse wheel goes to the next / previous image (Ctrl+wheel zooms)
tr.PrefWheel=Fare tekerleği sonraki / önceki görüntüye geçsin (Ctrl+tekerlek yakınlaştırır)
en.PrefSingle=Open images in the window that is already open
tr.PrefSingle=Görüntüleri zaten açık olan pencerede aç
en.PrefEsc=Esc key closes the window
tr.PrefEsc=Esc tuşu pencereyi kapatsın
en.ThemeTitle=Appearance
tr.ThemeTitle=Görünüm
en.ThemeDesc=Choose the colors and the layout of the window.
tr.ThemeDesc=Pencerenin renklerini ve düzenini seçin.
en.ThemeSub=Colors of the toolbar, side panel and menus:
tr.ThemeSub=Araç çubuğu, yan panel ve menülerin renkleri:
en.ThemeDark=Dark
tr.ThemeDark=Koyu
en.ThemeLight=Light
tr.ThemeLight=Açık
en.LayoutSub=Window layout:
tr.LayoutSub=Pencere düzeni:
en.LayoutFull=Toolbar, status bar and side panel (recommended)
tr.LayoutFull=Araç çubuğu, durum çubuğu ve yan panel (önerilen)
en.LayoutMinimal=Minimal: only the photo, everything on the right-click menu and the keyboard
tr.LayoutMinimal=Sade: yalnızca fotoğraf, her şey sağ tık menüsünde ve klavyede
en.RunDefaultApps=Open Windows settings to make Lucid Viewer the default photo viewer
tr.RunDefaultApps=Lucid Viewer'ı varsayılan fotoğraf görüntüleyici yapmak için Windows ayarlarını aç
en.DownloadFailed=The model could not be downloaded:%n%1%n%nSetup will continue. Lucid Viewer can download the model later, the first time you use the feature.
tr.DownloadFailed=Model indirilemedi:%n%1%n%nKurulum devam edecek. Lucid Viewer modeli daha sonra, özelliği ilk kullandığınızda indirebilir.
en.RemoveUserData=Do you also want to remove your Lucid Viewer settings, pinned photos, signature and downloaded models?
tr.RemoveUserData=Lucid Viewer ayarlarınız, sabitlediğiniz fotoğraflar, imzanız ve indirilen modeller de silinsin mi?
en.AppDescription=Fast image viewer and light editor.
tr.AppDescription=Hızlı görüntü görüntüleyici ve hafif düzenleyici.

[Types]
Name: "standard"; Description: "{cm:TypeStandard}"
Name: "full";     Description: "{cm:TypeFull}"
Name: "custom";   Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "main";            Description: "{cm:CompMain}"; Types: standard full custom; Flags: fixed
Name: "models";          Description: "{cm:CompModels}"; Types: full
Name: "models\bgremove"; Description: "{cm:CompBgRemove}"; Types: full; ExtraDiskSpaceRequired: 175997641

[Tasks]
Name: "desktopicon";   Description: "{cm:TaskDesktop}";      GroupDescription: "{cm:GroupShortcuts}"
Name: "startmenuicon"; Description: "{cm:TaskStartMenu}";    GroupDescription: "{cm:GroupShortcuts}"
Name: "convertmenu";   Description: "{cm:TaskConvertMenu}";  GroupDescription: "{cm:GroupExplorer}"
Name: "fileassoc";     Description: "{cm:TaskFileAssoc}";    GroupDescription: "{cm:GroupExplorer}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.lib,*.exp,*.ilk,*.iobj,*.ipdb,*.log"; Flags: ignoreversion; Components: main
Source: "..\docs\brand\LucidViewer.ico"; DestDir: "{app}"; Flags: ignoreversion; Components: main
; Downloaded in NextButtonClick (wpReady), copied from the temporary folder
Source: "{tmp}\{#BgModelFile}"; DestDir: "{app}\models"; Flags: external ignoreversion skipifsourcedoesntexist; Components: models\bgremove

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: startmenuicon
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Uninstall must not leave the tray tool in the Run key
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AutostartValue}"; ValueData: """{app}\{#AppExe}"" /tray"; Check: PrefOn(2); Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#AutostartValue}"; Flags: uninsdeletevalue dontcreatekey

; ProgID used for all image types
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
; Capabilities, so Windows lists the program in Default apps
Root: HKA; Subkey: "Software\{#AppId}"; Flags: uninsdeletekeyifempty; Tasks: fileassoc
Root: HKA; Subkey: "Software\{#AppId}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\{#AppId}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:AppDescription}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\{#AppId}\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "Software\{#AppId}\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassoc

; One set of entries per supported file type
#define ExtList "jpg;jpeg;jpe;jfif;png;gif;bmp;dib;tif;tiff;webp;jxl;avif;heif;heic;tga;qoi;psd;psb;ico;wdp;hdp;jxr;pef;dng;crw;nef;cr2;cr3;mrw;rw2;orf;x3f;arw;kdc;nrw;dcr;sr2;raf"
#define Rest ExtList
#define Ext ""
#sub EmitExt
  #define Ext (Pos(";", Rest) > 0 ? Copy(Rest, 1, Pos(";", Rest) - 1) : Rest)
Root: HKA; Subkey: "Software\{#AppId}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#Ext}"; ValueData: "{#ProgId}"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.{#Ext}\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".{#Ext}"; ValueData: ""; Tasks: fileassoc
#endsub
#for {Rest = ExtList; Len(Rest) > 0; Rest = (Pos(";", Rest) > 0 ? Copy(Rest, Pos(";", Rest) + 1) : "")} EmitExt

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "/registerconvertmenu"; Flags: runhidden waituntilterminated runasoriginaluser; Tasks: convertmenu
Filename: "{app}\{#AppExe}"; Parameters: "/unregisterconvertmenu"; Flags: runhidden waituntilterminated runasoriginaluser; Tasks: not convertmenu
Filename: "{code:DefaultAppsUri}"; Description: "{cm:RunDefaultApps}"; Flags: shellexec postinstall skipifsilent nowait; Tasks: fileassoc
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: postinstall skipifsilent nowait runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "/unregisterconvertmenu"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterConvertMenu"

[Code]
var
  DownloadPage: TDownloadWizardPage;
  PrefPage: TInputOptionWizardPage;
  ThemePage: TInputOptionWizardPage;

const
  PREF_STARTSCREEN = 0;
  PREF_RECENT = 1;
  PREF_SCREENSHOT = 2;
  PREF_SHOTHISTORY = 3;
  PREF_WHEEL = 4;
  PREF_SINGLE = 5;
  PREF_ESC = 6;
  PREF_COUNT = 7;

function UserIni: String;
begin
  Result := ExpandConstant('{userappdata}\{#AppDataName}\{#IniName}');
end;

// Default of a check box: the choice of the last setup, else the user's current setting, else Default
function PrefDefault(Index: Integer; const IniKey, TrueValue: String; Default: Boolean): Boolean;
var
  S: String;
begin
  S := GetPreviousData('Pref' + IntToStr(Index), '');
  if S <> '' then
    Result := S = '1'
  else if (IniKey <> '') and FileExists(UserIni) and (GetIniString('{#IniSection}', IniKey, '', UserIni) <> '') then
    Result := CompareText(GetIniString('{#IniSection}', IniKey, '', UserIni), TrueValue) = 0
  else
    Result := Default;
end;

function WindowsUsesLightTheme: Boolean;
var
  V: Cardinal;
begin
  Result := RegQueryDWordValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize', 'AppsUseLightTheme', V) and (V = 1);
end;

function PrefOn(Index: Integer): Boolean;
begin
  if PrefPage = nil then
    Result := False
  else
    Result := PrefPage.Values[Index];
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
var
  I: Integer;
begin
  for I := 0 to PREF_COUNT - 1 do
    if PrefPage.Values[I] then
      SetPreviousData(PreviousDataKey, 'Pref' + IntToStr(I), '1')
    else
      SetPreviousData(PreviousDataKey, 'Pref' + IntToStr(I), '0');
  if ThemePage.CheckListBox.Checked[2] then
    SetPreviousData(PreviousDataKey, 'Theme', 'light')
  else
    SetPreviousData(PreviousDataKey, 'Theme', 'dark');
  if ThemePage.CheckListBox.Checked[5] then
    SetPreviousData(PreviousDataKey, 'Layout', 'minimal')
  else
    SetPreviousData(PreviousDataKey, 'Layout', 'full');
end;

procedure SetIni(const Key, Value: String);
begin
  SetIniString('{#IniSection}', Key, Value, ExpandConstant('{app}\{#IniName}'));
end;

function BoolStr(B: Boolean): String;
begin
  if B then Result := 'true' else Result := 'false';
end;

// Writes the choices into the settings file next to the program. They are the defaults for every user,
// the user's own settings file (Tools > Settings) still wins.
procedure WritePreferences;
begin
  SetIni('ShowStartScreen', BoolStr(PrefOn(PREF_STARTSCREEN)));
  SetIni('RememberRecentFiles', BoolStr(PrefOn(PREF_RECENT)));
  if PrefOn(PREF_SCREENSHOT) then
  begin
    SetIni('ScreenshotHotkey', 'PrintScreen');
    SetIni('ScreenshotTextHotkey', 'Shift+PrintScreen');
  end else begin
    SetIni('ScreenshotHotkey', '');
    SetIni('ScreenshotTextHotkey', '');
  end;
  SetIni('ScreenshotHistory', BoolStr(PrefOn(PREF_SCREENSHOT) and PrefOn(PREF_SHOTHISTORY)));
  SetIni('NavigateWithMouseWheel', BoolStr(PrefOn(PREF_WHEEL)));
  SetIni('SingleInstance', BoolStr(PrefOn(PREF_SINGLE)));
  SetIni('EscapeClosesWindow', BoolStr(PrefOn(PREF_ESC)));
  if ThemePage.CheckListBox.Checked[2] then
    SetIni('ChromeTheme', 'light')
  else
    SetIni('ChromeTheme', 'dark');
  SetIni('ShowToolbar', BoolStr(not ThemePage.CheckListBox.Checked[5]));
  SetIni('ShowSidePanel', BoolStr(not ThemePage.CheckListBox.Checked[5]));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    WritePreferences;
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
var
  Light, Minimal: Boolean;
  Theme: String;
begin
  PrefPage := CreateInputOptionPage(wpSelectTasks, CustomMessage('PrefTitle'), CustomMessage('PrefDesc'), CustomMessage('PrefSub'), False, False);
  PrefPage.Add(CustomMessage('PrefStartScreen'));
  PrefPage.Add(CustomMessage('PrefRecent'));
  PrefPage.Add(CustomMessage('PrefScreenshot'));
  PrefPage.AddEx(CustomMessage('PrefShotHistory'), 1, False);
  PrefPage.Add(CustomMessage('PrefWheel'));
  PrefPage.Add(CustomMessage('PrefSingle'));
  PrefPage.Add(CustomMessage('PrefEsc'));
  PrefPage.Values[PREF_STARTSCREEN] := PrefDefault(PREF_STARTSCREEN, 'ShowStartScreen', 'true', True);
  PrefPage.Values[PREF_RECENT] := PrefDefault(PREF_RECENT, 'RememberRecentFiles', 'true', True);
  PrefPage.Values[PREF_SCREENSHOT] := PrefDefault(PREF_SCREENSHOT, '', '', False);
  PrefPage.Values[PREF_SHOTHISTORY] := PrefDefault(PREF_SHOTHISTORY, 'ScreenshotHistory', 'true', False);
  PrefPage.Values[PREF_WHEEL] := PrefDefault(PREF_WHEEL, 'NavigateWithMouseWheel', 'true', False);
  PrefPage.Values[PREF_SINGLE] := PrefDefault(PREF_SINGLE, 'SingleInstance', 'true', False);
  PrefPage.Values[PREF_ESC] := PrefDefault(PREF_ESC, 'EscapeClosesWindow', 'true', False);

  // Theme: last setup's choice, else the user's setting, else follow the Windows app theme
  Theme := GetPreviousData('Theme', '');
  if (Theme = '') and FileExists(UserIni) then
    Theme := GetIniString('{#IniSection}', 'ChromeTheme', '', UserIni);
  if Theme = '' then
    Light := WindowsUsesLightTheme
  else
    Light := CompareText(Theme, 'light') = 0;
  Minimal := GetPreviousData('Layout', 'full') = 'minimal';

  // Two radio groups in one list: 0 colors, 1 dark, 2 light, 3 layout, 4 full, 5 minimal
  ThemePage := CreateInputOptionPage(PrefPage.ID, CustomMessage('ThemeTitle'), CustomMessage('ThemeDesc'), '', False, False);
  ThemePage.CheckListBox.AddGroup(CustomMessage('ThemeSub'), '', 0, nil);
  ThemePage.CheckListBox.AddRadioButton(CustomMessage('ThemeDark'), '', 1, not Light, True, nil);
  ThemePage.CheckListBox.AddRadioButton(CustomMessage('ThemeLight'), '', 1, Light, True, nil);
  ThemePage.CheckListBox.AddGroup(CustomMessage('LayoutSub'), '', 0, nil);
  ThemePage.CheckListBox.AddRadioButton(CustomMessage('LayoutFull'), '', 1, not Minimal, True, nil);
  ThemePage.CheckListBox.AddRadioButton(CustomMessage('LayoutMinimal'), '', 1, Minimal, True, nil);

  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
end;

// The model is already there (earlier install, or downloaded by the program itself)
function ModelPresent(const FileName: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\models\' + FileName)) or
            FileExists(ExpandConstant('{localappdata}\{#AppDataName}\models\' + FileName)) or
            FileExists(ExpandConstant('{localappdata}\JPEGView\models\' + FileName));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and WizardIsComponentSelected('models\bgremove') and not ModelPresent('{#BgModelFile}') then
  begin
    DownloadPage.Clear;
    DownloadPage.Add('{#BgModelUrl}', '{#BgModelFile}', '{#BgModelSha256}');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
      except
        if not DownloadPage.AbortedByUser then
          SuppressibleMsgBox(FmtMessage(CustomMessage('DownloadFailed'), [GetExceptionMessage]), mbError, MB_OK, IDOK);
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

function DefaultAppsUri(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := 'ms-settings:defaultapps?registeredAppMachine={#AppName}'
  else
    Result := 'ms-settings:defaultapps?registeredAppUser={#AppName}';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
  begin
    if MsgBox(CustomMessage('RemoveUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      DelTree(ExpandConstant('{userappdata}\{#AppDataName}'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\{#AppDataName}'), True, True, True);
    end;
  end;
end;
