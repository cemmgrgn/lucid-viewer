Lucid Viewer runs on 64-bit Windows 10 version 1809 or later, and on Windows 11.

## Download

Get the files from the [latest release](https://github.com/cemmgrgn/lucid-viewer/releases/latest). There are two:

- `LucidViewer-<version>-Setup-x64.exe`: the installer. Use this for most people.
- `LucidViewer-<version>-portable-x64.zip`: the portable version. See [Portable version](#portable-version) below.
- `LucidViewer-<version>-Setup-arm64.exe` and `LucidViewer-<version>-portable-arm64.zip`: the native versions for
  Windows on ARM (Snapdragon PCs). The x64 versions run there too, in emulation.

## Run the installer

1. Start the setup file. Windows may show a SmartScreen warning. See [[FAQ|FAQ]].
2. Choose who the program is installed for: only you, or all users. All users needs administrator rights.
3. Choose the components:
   - **Lucid Viewer** is always installed.
   - **Optional AI models** is for background removal. The model (U²-Net, about 168 MB) is downloaded during setup. If the download fails, setup continues and Lucid Viewer downloads the model the first time you use the feature.
4. Choose the Windows integration options:
   - Desktop shortcut
   - Start menu shortcut
   - **Convert to** in the Explorer right-click menu for images
   - Register Lucid Viewer as a photo viewer, so it shows in *Open with* and *Default apps*
5. The setup also asks about the start screen, the screenshot tool on the Print Screen key, and the theme and layout. You can change all of these later in **Tools > Settings**.

To make Lucid Viewer the default viewer, open **Windows Settings > Default apps** and pick Lucid Viewer for the file types you want. The setup can open this page for you at the end.

## Portable version

Extract the zip file to any folder and run `LucidViewer.exe`. The settings are stored next to the program, because the portable zip sets `StoreToEXEPath=true`.

Do not put the portable folder in *Program Files*, because the settings are written next to the program. A USB stick works well.

## Windows package manager

```
winget install CemGirgin.LucidViewer
```

This command works once the package has been accepted into the winget repository. Until then, use the installer.

## Silent install

Use these switches to install without any window. They are meant for administrators and scripts.

| Switch | What it does |
|---|---|
| `/VERYSILENT` | Installs without any window |
| `/SILENT` | Installs with a progress window only |
| `/CURRENTUSER` | Installs for the current user only |
| `/ALLUSERS` | Installs for all users |
| `/COMPONENTS="..."` | Chooses the components, for example `main,models\bgremove` |
| `/TASKS="..."` | Chooses the Windows integration options, for example `desktopicon,startmenuicon,convertmenu,fileassoc` |

Example:

```
LucidViewer-<version>-Setup-x64.exe /VERYSILENT /CURRENTUSER /COMPONENTS="main,models\bgremove" /TASKS="desktopicon,startmenuicon,convertmenu,fileassoc"
```

The task names are the ones used in the installer script. Leave out a name to skip that option.

## Uninstall

Open **Settings > Apps**, select Lucid Viewer and choose **Uninstall**. The uninstaller asks if you also want to remove your settings, pinned photos, signature and downloaded models. Choose *No* to keep them.

## Moving from JPEGView

On the first start, Lucid Viewer copies your JPEGView settings, shortcuts and signature from `%APPDATA%\JPEGView` to `%APPDATA%\LucidViewer`. The JPEGView folder is not changed. You can keep using JPEGView next to Lucid Viewer.

## SmartScreen warning

Releases are not code signed yet, so Windows SmartScreen can show a warning when you start the installer. Download the file only from the release page of this repository. The status of code signing is on the [[Code signing and privacy|Code-signing-and-privacy]] page.
