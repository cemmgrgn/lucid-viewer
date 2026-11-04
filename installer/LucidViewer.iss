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
  #define SourceDir    "..\src\JPEGView\bin\x64\Release"
#endif
#ifndef AppExe
  #define AppExe       "LucidViewer.exe"
#endif
; Folder name the program uses under %APPDATA% and %LOCALAPPDATA%
#ifndef AppDataName
  #define AppDataName  "Lucid Viewer"
#endif
; Mutexes the running program holds (main window and screenshot tray)
#ifndef AppMutexes
  #define AppMutexes   "JPVMtX2869,JPEGViewScreenshotTray"
#endif
; Name of the HKCU\...\Run value the tray tool writes for "Start with Windows"
#ifndef AutostartValue
  #define AutostartValue "JPEGView Screenshot"
#endif
#ifndef AppVersion
  #define AppVersion   GetVersionNumbersString(AddBackslash(SourceDir) + AppExe)
#endif

#define ProgId         "LucidViewer.Image"

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
LicenseFile=..\LICENSE.txt
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
VersionInfoVersion={#AppVersion}
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
en.TaskAutostart=Start the screenshot tool (PrtScn) with Windows
tr.TaskAutostart=Ekran görüntüsü aracını (PrtScn) Windows ile başlat
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
Name: "autostart";     Description: "{cm:TaskAutostart}";    GroupDescription: "{cm:GroupExplorer}"; Flags: unchecked

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
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AutostartValue}"; ValueData: """{app}\{#AppExe}"" /tray"; Tasks: autostart; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#AutostartValue}"; Flags: uninsdeletevalue dontcreatekey

; ProgID used for all image types
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc
; Capabilities, so Windows lists the program in Default apps
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

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
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
