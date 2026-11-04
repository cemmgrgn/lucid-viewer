Most settings are in **Tools → Settings**, a searchable window that explains each option and applies changes right away. This page lists every key of the settings file for those who prefer a text editor.

- **Global defaults:** `LucidViewer.ini` next to `LucidViewer.exe` (do not edit it in *Program Files*, it is replaced by updates).
- **Your settings:** `%APPDATA%\LucidViewer\LucidViewer.ini`, section `[LucidViewer]`. Only the keys you change are written there; everything else comes from the global file.
- **Portable:** with `StoreToEXEPath=true` (set in the portable zip) settings stay next to the program.

## Program Options

| Key | Default | Description |
|---|---|---|
| `StoreToEXEPath` | `false` | Only set this to true if writing to the EXE directory makes sense, e.g. on an installation on an USB memory stick. Never set it to true when the EXE is located in the 'Program Files' folder, with Windows versions newer than Windows XP this would not work! |
| `SingleInstance` | `false` | If set to true, only one single instance of Lucid Viewer runs at any time, if false multiple instances are allowed. Set to true to open all images in the same Lucid Viewer window. |
| `SkipFileOpenDialogOnStartup` | `false` | Set to true to skip the 'File Open' dialog when starting Lucid Viewer without providing a file name as parameter |
| `FilesProcessedByWIC` | `*.wdp;*.hdp;*.jxr` | File endings of files to be decoded by WIC (Windows Image Converter). If the Microsoft Camera Codec pack is installed, full size camera RAW files can be read with WIC. Add the file endings of the raw files to view here, e.g. FilesProcessedByWIC=*.wdp;*.hdp;*.jxr;*.nef |
| `FileEndingsRAW` | `*.pef;*.dng;*.crw;*.nef;*.cr2;*.mrw;*.rw2;*.orf;*.x3f;*.arw;*.kdc;*.nrw;*.dcr;*.sr2;*.raf;*.kc2;*.erf;*.3fr;*.raw;*.mef;*.mos;*.mdc;*.cr3` | File endings of camera RAW files to be searched for embedded JPEG thumb images to display. Reading just these embedded JPEGs is much faster than decoding the RAW using WIC |
| `DisplayFullSizeRAW` | `0` | Set behavior for opening RAW files listed in FileEndingsRAW. 0: open embedded thumbnail. 1: open full size. 2: open thumbnail, fallback to full size. 3: open full size, fallback to thumbnail |
| `KeepParameters` | `false` | Set to true to keep the zoom, pan, contrast, gamma, sharpen and rotation setting between the images |
| `DisplayMonitor` | `-1` | Only for multi-monitor systems! Monitor to start the application on. -1: Use monitor with largest resolution, primary monitor if several monitors have the same resolution. 0: Use primary monitor. 1...n: Use the non-primary monitor with index n |
| `CPUType` | `AutoDetect` | CPUType can be AutoDetect, Generic, MMX, SSE or AVX2. Generic should work on all CPUs, MMX needs at least MMX II (starting from PIII). Use AutoDetect to detect the best possible algorithm to use |
| `CPUCoresUsed` | `0` | Number of CPU cores used. Set to 0 for auto detection. Must be 1 to 4, or 0 for auto detect. |
| `IniEditor` | `notepad` | Editor for INI files notepad : Use notepad.exe system : Use application registered for INI files other : Full path to application for editing INI files, %exepath% placeholder can be used |
| `WallpaperPath` | `%temp%` | When setting a processed image as desktop wallpaper, the processed image must be saved somewhere. This INI setting sets the path to save the file. Default is %temp%, the system TEMP directory. You must have write access to the specified directory! |
| `AllowFileDeletion` | `true` | Set to false to disallow deletion of images in Lucid Viewer. This hides the 'x' button on the navigation panel and disables the commands for deletion (IDM_MOVE_TO_RECYCLE_BIN etc.) |

## User Interface Options

| Key | Default | Description |
|---|---|---|
| `Language` | `auto` | Specify language used in the user interface. Set to 'auto' to use the language of the operating system. Lucid Viewer will auto-detect and use the ISO 3166-1 country code (two letters) if the file exists: strings_&lt;language>-&lt;country>.txt otherwise, fallback to just the ISO 639-1 language code if the file exists: strings_&lt;language>.txt. Currently supported languages: 'be'    Belarusian. 'bg'    Bulgarian. 'cs'    Czech. 'de'    German. 'el'    Greek. 'en'    English (default). 'es'    Spanish (Spain). 'es-ar' Spanish (Argentina). 'eu'    Basque. 'fi'    Finnish. 'fr'    French. 'hu'    Hungarian. 'it'    Italian. 'ja'    Japanese. 'ko'    Korean. 'pl'    Polish. 'pt'    Portuguese. 'pt-br' Portuguese (Brazil). 'ro'    Romanian. 'ru'    Russian. 'sk'    Slovak. 'sl'    Slovenian. 'sr'    Serbian. 'sv'    Swedish. 'ta'    Tamil. 'tr'    Turkish. 'uk'    Ukrainian. 'zh'    Chinese, Simplified. 'zh-tw' Chinese, Traditional (Taiwan) |
| `BackgroundColor` | `0 0 0` | Background color, R G B, each component must be in [0..255], e.g. "128 128 128" for a middle gray |
| `GUIColor` | `243 242 231` | GUI colors, R G B format as used by BackgroundColor |
| `HighlightColor` | `255 205 0` | Like `GUIColor` above. |
| `SelectionColor` | `255 205 0` | Like `GUIColor` above. |
| `SliderColor` | `255 0 80` | Like `GUIColor` above. |
| `FileNameColor` | `255 255 255` | Like `GUIColor` above. |
| `DefaultGUIFont` | `Default` | GUI font, format: "font name" fontSizeInPoints [bold]. 'Default' for the default GUI font of Windows. Example: DefaultGUIFont="Arial" 9.0 bold |
| `AllowEditGlobalSettings` | `false` | Set to true to show the menu item that allows editing the global INI file option is found under Settings/Admin sub-menu |
| `ShowFullScreen` | `false` | Start in full screen or windowed mode. 'true' or 'false' to always start in full screen, respectively windowed mode. 'auto' to choose best mode depending on image size of first image - windowed when it is small, full screen when it is large |
| `SingleFullScreenInstance` | `true` | If set to true, only one single instance of Lucid Viewer runs in full screen mode, if false multiple instances are allowed. The value is ignored if SingleInstance=true |
| `DefaultWindowRect` | `image` | Sets the default position and size of the window in window mode. Possible values: 'auto' for 2/3 of screen size. 'max' to start with maximized window. 'image' to adjust the window size automatically to the image size. 'sticky' to automatically restore the last used window size (when ShowFullScreen=auto only the upper,left position is restored). 'left top right bottom', space separated e.g: 100 100 900 700 |
| `StickyWindowRect` | *(empty)* | Contains the stored window rectangle in case of DefaultWindowRect=sticky |
| `MinimalWindowSize` | `320 240` | Minimal window size in windowed mode (in pixels) |
| `WindowBorderlessOnStartup` | `false` | If set to 'true', window starts in borderless mode (title bar hidden, cannot be manually resized). The window mode can be changed after startup |
| `WindowAlwaysOnTopOnStartup` | `false` | If set to 'true', window starts in always-on-top mode (window will always be visible on top of other windows). The window mode can be changed after startup |
| `ShowToolbar` | `true` | The toolbar (with the main menu), the status bar and the information side panel are shown next to the image, never over it. They are hidden in full screen mode and when the window title bar is hidden. They can also be switched on and off in the View menu, in the context menu and with Ctrl+T (toolbar), Ctrl+I (side panel). |
| `ShowStatusBar` | `true` | Like `ShowToolbar` above. |
| `ShowSidePanel` | `true` | Like `ShowToolbar` above. |
| `SidePanelCollapsed` | `false` | Side panel collapsed to a narrow strip with icons (toggled with its arrow button) |
| `EscapeClosesWindow` | `false` | If true, the Esc key closes Lucid Viewer also in window mode (as in older versions). If false, Esc ends the current tool (crop, drawing, compare) and leaves full screen mode. |
| `ShowStartScreen` | `true` | Start screen with the last opened photos and the photos pinned by the user, shown when no image is open (Alt+Home). If false, the file open dialog is shown when Lucid Viewer is started without an image. |
| `RememberRecentFiles` | `true` | Remember the recently opened photos and show them on the start screen. If false, nothing is written to. StartRecent.txt and the recent row stays empty. |
| `MinDisplaySize` | `600` | Small images are shown zoomed in so that their larger side has at least this size (pixels at 96 DPI) when the zoom mode does not enlarge images. 0 shows them in their original size. |
| `SmoothAnimations` | `true` | Smooth zooming with the mouse wheel and the zoom keys, smooth scrolling of the start screen and the thumbnail strip |
| `SidePanelWidth` | `290` | Width of the information side panel in pixels (at 96 DPI), 180 to 800 |
| `ChromeTheme` | `dark` | Colors of toolbar, status bar, side panel and menus: dark or light |
| `ConvertCustomPresets` | *(empty)* | Additional conversions for the 'Convert to' menus, separated by semicolons. A conversion is 'format' or 'operation:format' with the formats jpg, png, webp, webpll (lossless), bmp, tiff, gif, ico, pdf and same (format of the source file) and the operations scaleN (N percent), fitN (long edge at most N pixels), rotl, rotr, rot180 and nometa (no EXIF/GPS). Example: ConvertCustomPresets=fit2560:webp;scale30:jpg. Run LucidViewer.exe /registerconvertmenu (or Tools menu) again after changing this to update the Explorer menu. |
| `ConvertOutputFolder` | *(empty)* | Folder for converted files. Empty: next to the source file. A relative name such as 'converted' is a sub folder of the source folder (created when needed), an absolute path is used for all files. |
| `ScreenshotFolder` | *(empty)* | Folder of saved screenshots. Empty: Pictures\Screenshots |
| `ScreenshotHotkey` | `PrintScreen` | Global shortcuts of the screenshot tool in the notification area, e.g. PrintScreen, Shift+PrintScreen, Ctrl+Alt+S, F12. Empty switches a shortcut off. Windows 11 gives the Print Screen key to the Snipping Tool unless this is switched off in. Settings > Accessibility > Keyboard. |
| `ScreenshotTextHotkey` | `Shift+PrintScreen` | Like `ScreenshotHotkey` above. |
| `ScreenshotHistory` | `false` | Screenshot history: if true, every screenshot is kept in the screenshot folder (also the ones only copied to the clipboard) and the start screen of the viewer shows them in a row. |
| `OcrLanguage` | *(empty)* | Language of the text recognition (OCR), e.g. tr or en-US. The language must be installed in Windows. Empty: the language of a non English keyboard layout, else the Windows display language. |
| `ShowBottomPanel` | `true` | Set to true to show the bottom panel when moving the mouse to the bottom of the screen/window. The bottom panel provides basic image processing functionality |
| `ShowNavPanel` | `true` | Set to false if the navigation panel shall not be blended to the image |
| `ShowZoomNavigator` | `true` | Set to false to disable the thumbnail image blended in during zoom and pan |
| `BlendFactorNavPanel` | `0.5` | Blending factor of the navigation panel when the mouse is not on that panel. Set to 0.0 to only show the panel when the mouse is over the panel |
| `ScaleFactorNavPanel` | `1.0` | Scaling factor for the navigation panel. Increase if the buttons on the panel are too small, e.g. on a touchscreen. |

## File Navigation Options

| Key | Default | Description |
|---|---|---|
| `FileDisplayOrder` | `LastModDate` | Sorting order of the files when displaying the image files in a folder. Can be LastModDate, CreationDate, FileName, FileSize, Random or Rating (star rating) |
| `FileSortAscending` | `true` | Sort files ascending (increasing, e.g. A->Z, 0->9) or descending (decreasing, e.g. Z->A, 9->0) |
| `FolderNavigation` | `LoopFolder` | Navigation within or between folders. LoopFolder : Loop within the source folder and never leave this folder. LoopSameFolderLevel: Loop to next folder on the same hierarchy level (sibling folders). LoopSubFolders: Loop into subfolders of the source folder |
| `WrapAroundFolder` | `true` | If true the files in a folder are shown with wrap around, thus going from last to first image and vice versa. If false navigation stops on the last and first image |
| `FlashWindowAlert` | `true` | When WrapAroundFolder is false, alert when trying to go to the next file while at the end of the file list, or previous file while at the beginning of file list. If true, window flashes briefly when there are no more new images to show. If false, there is no visual feedback |
| `BeepSoundAlert` | `false` | When WrapAroundFolder is false, alert when trying to go to the next file while at the end of the file list, or previous file while at the beginning of file list. If true, system emits a short beep using the default system sound when there are no more new images to show. If false, there is no audio feedback |
| `NavigateWithMouseWheel` | `false` | If true, the mouse wheel can be used to navigate forward and backward and zoom must be done with Ctrl-MouseWheel. If false, zoom is done with the mousewheel (no Ctrl required) |
| `ExchangeXButtons` | `true` | If true, the extended mouse buttons (Forward and backward) are reversed compared to Internet explorer. This is useful to put the 'go to next image' functionality to the button that is better reachable e.g. Hitting Backwards button goes Forward in Lucid Viewer |

## User Input Config

| Key | Default | Description |
|---|---|---|
| `DefaultSelectionMode` | `true` | By default, Left-click and drag enables selection mode whenever pan is not available.  CTRL+Left-click and drag enables selection mode when zoomed in. Set this to false to only use selection mode when CTRL is pressed |
| `MouseWheelZoomSpeed` | `1.0` | Zoom speed with mouse wheel, 1.0 means default speed, smaller values slower zoom, larger faster zoom |

## Image Display Options

| Key | Default | Description |
|---|---|---|
| `ReloadWhenDisplayedImageChanged` | `true` | If true, reloads the currently displayed image automatically when it is changed by an external program on disk. Also reloads the image list when a file in the current directory is added or deleted. Set to false to turn off this behavior. |
| `HighQualityResampling` | `true` | Set to true to use high quality sampling as default. |
| `DownSamplingFilter` | `BestQuality` | DownSamplingFilter can be BestQuality, NoAliasing or Narrow. The BestQuality filter produces a very small amount of aliasing. The NoAliasing filter is a Lanczos filter that has almost no aliasing when sharpen is set to zero. The Narrow filter produces quite a lot of aliasing but will sharpen much and also sharpens 100% images |
| `AutoRotateEXIF` | `true` | If true, JPEG images are auto rotated according to EXIF image orientation tag if present. |
| `MinimalDisplayTime` | `0` | Minimal display time of an image when manually navigating (with keyboard or mouse) (in milliseconds [0..1000 ms]) |
| `ForceGDIPlus` | `false` | Force using GDI+ for reading JPEG. Only use when you have problems reading your JPEGs with the default Turbo JPEG library. Note that using GDI+ is slower than the Turbo JPEG library! (Setting to true automatically disables Animated PNG support) |
| `UseEmbeddedColorProfiles` | `false` | If true, embedded ICC color profiles are used for JPEG, PNG and TIFF. This forces using GDI+ and therefore results in much slower loading of images! Only set to true if you really need this. (ICC color profiles are not supported for Animated PNG) |
| `TransparencyColor` | `0 0 0` | Color used for transparent parts of images, R G B format as used by BackgroundColor |
| `AutoZoomMode` | `FitNoZoom` | Auto zoom mode (for window mode only if AutoZoomModeFullscreen is present). FitNoZoom : Fit images to screen, never enlarge image. FillNoZoom : Fill screen with no black borders, crop if necessary but not too much, never enlarge image. Fit : Fit images to screen. Fill : Fill screen with no black borders, crop if necessary |
| `AutoZoomModeFullscreen` | *(empty)* | Auto zoom mode for fullscreen mode. If empty, same value as 'AutoZoomMode' is used. |
| `ZoomPausePercent` | `100` | For convenience, when zooming continuously, it will pause at the set percent (default 100%) briefly before continuing in either direction. This ensures that you can always zoom in or zoom out to the set percent reliably regardless of image size or zoom steps default: 100 set to 0 to disable the pause |
| `Contrast` | `0.0` | Contrast correction to apply to all images. Must be in -0.5 .. 0.5. Values > 0 increase contrast, values &lt; 0 decrease contrast |
| `Gamma` | `1.0` | Gamma correction to apply to all images. Must be between 0.5 and 2. Use gamma&lt;1 to increase brightness and gamma>1 to decrease brightness |
| `Saturation` | `1.0` | Color saturation to apply to all images. Must be in 0.0 .. 2.0. 0.0 means gray scale image, 1.0 no additional color saturation, 2.0 maximal saturation |
| `Sharpen` | `0.3` | Sharpening to apply for downsampled images. Must be in 0 .. 0.5. Note that for 100 % zoom, the BestQuality filter will not apply any sharpening, only the other filters do |

## Default Image Editing Options

| Key | Default | Description |
|---|---|---|
| `UnsharpMaskParameters` | `1.0 1.0 4.0` | Default parameters for unsharp masking: Radius Amount Threshold. Note: unsharp masking cannot be applied automatically to every image. This setting only provides the default parameters when entering the unsharp mask mode |
| `RTShowGridLines` | `true` | Default parameters for controlling rotation and perspective correction |
| `RTAutoCrop` | `true` | Like `RTShowGridLines` above. |
| `RTPreserveAspectRatio` | `true` | Like `RTShowGridLines` above. |
| `CyanRed` | `0.0` | Default color balance. Negative values for C,M,Y correction, positive for R,G,B. Values must be in -1.0 .. +1.0 |
| `MagentaGreen` | `0.0` | Like `CyanRed` above. |
| `YellowBlue` | `0.0` | Like `CyanRed` above. |
| `DefaultFixedCropSize` | `320 200` | The initial crop window size when using 'Fixed Size' crop mode |
| `UserCropAspectRatio` | `14 11` | User defined crop aspect ratio (x y), meaning (x : y). Shows up as last item in crop popup menu |

## Image Info Display Options

| Key | Default | Description |
|---|---|---|
| `ShowEXIFDateInTitle` | `true` | Show the acquisition date from EXIF data in the window title |
| `ShowFilePathInTitle` | `false` | Show full file path and name in the window title (shows only file name by default) |
| `ShowFileName` | `false` | Set to true to initially display the file name of each image in the upper, left corner of the screen |
| `FileNameFormat` | `%index%  %filepath%` | The elements to show when showing the file name. Possible elements: %filename% : File name. %filepath% : File name and path. %index%    : Index of image in folder, e.g. [1/12]. %zoom%     : Current zoom factor. %size%     : Size of image in pixels (w x h). %filesize% : Size of image on disk |
| `FileNameFont` | `Default` | Font for the file name display, see DefaultGUIFont key for the format |
| `ShowFileInfo` | `false` | Set to true to initially display the file info box (EXIF info if available) |
| `ShowJPEGComments` | `true` | Set to true to show JPEG comments (EXIF user comment, EXIF image description or JPEG comment) in the file info box |
| `ShowHistogram` | `false` | Set to true to show the histogram on the file info panel by default |
| `GPSMapProvider` | `https://opentopomap.org/#marker=15/{lat}/{lng}` | URL for the map provider to display GPS coordinates on a map (only shown when EXIF has GPS coordinates). OpenStreetmap: www.openstreetmap.org/?mlat={lat}&mlon={lng}&zoom=15. Google maps: https://www.google.com/maps/search/?api=1&query={lat},{lng} |

## Automatic Correction Options

| Key | Default | Description |
|---|---|---|
| `AutoContrastCorrection` | `false` | Automatic contrast correction by histogram equalization. F5 enables/disables the correction on the current image. |
| `ACCExclude` | *(empty)* | Using the following two keys, it is possible to explicitly exclude/include folders from the contrast correction. More specific patterns have precedence over less specific patterns and inclusion has precedence over exclusion if a folder matches both. Example: '*\pics\orig\* has precedence over *\pics\* because it is more specific. Use the ; character to separate two expressions. Example: ACCExclude=%mypictures%\Digicam\edited\*;*.bmp. This will exclude all files in the ..\My Pictures\Digicam\edited\ folder and all its subfolders and all bmp files from automatic contrast correction. The following two placeholders are recognized: %mypictures%  : "My documents\My Pictures" folder. %mydocuments% : "My documents" folder |
| `ACCInclude` | *(empty)* | Like `ACCExclude` above. |
| `AutoContrastCorrectionAmount` | `0.5` | Amount of automatic contrast correction. 0 means no contrast correction, 1 means full (to black and white point) contrast correction. Must be in (0 .. 1) |
| `ColorCorrection` | `R: 0.2 G: 0.1 B: 0.35 C: 0.1 M: 0.3 Y: 0.15` | Amount of color correction in the color channels reg, green, blue, cyan, magenta and yellow. The numbers must be between 0.0 (no correction) and 1.0 (total correction towards the gray world model). Set all values to 0 to disable color correction |
| `AutoBrightnessCorrectionAmount` | `0.2` | Amount of automatic brightness correction. 0 means no brightness correction, 1 means full correction to middle gray. Must be in (0 .. 1) |
| `LocalDensityCorrection` | `false` | Automatic correction of local density (local brightness of images). Can be enabled/disabled on the image with F6 |
| `LDCExclude` | *(empty)* | See remark about exclusion/inclusion at the ACCExclude setting. The same applies to these settings. |
| `LDCInclude` | *(empty)* | Like `LDCExclude` above. |
| `LDCBrightenShadows` | `0.5` | Amount of local density correction of shadows. Can be in [0, 1] |
| `LDCBrightenShadowsSteepness` | `0.5` | Deep shadow steepness of enhancement. Can be in [0, 1], values bigger than 0.9 are not recommended |
| `LDCDarkenHighlights` | `0.25` | Amount of local density correction of highlights. Can be in [0, 1] |
| `LandscapeMode` | `false` | Set to true to turn on landscape enhancement mode automatically |
| `LandscapeModeParams` | `-1 -1 -1 -1 0.5 1.0 0.75 0.4 -1 -1 -1 -1` | Parameter set used in landscape enhancement mode. Space separated, use -1 to leave parameter untouched. Contrast Gamma Sharpen ColorCorrection ContrastCorrection LightenShadows DarkenHighlights DeepShadows CyanRed MagentaGreen YellowBlue Saturation |

## Slideshow Options

| Key | Default | Description |
|---|---|---|
| `MaxSlideShowFileListSizeKB` | `200` | Maximum size of slide show text files in KB |
| `SlideShowTransitionEffect` | `Blend` | Transition effect for slide shows in full screen mode - ignored when used in window mode. Possible transition effects: None, Blend, SlideRL, SlideLR, SlideTB, SlideBT, RollRL, RollLR, RollTB, RollBT, ScrollRL, ScrollLR, ScrollTB, ScrollBT |
| `SlideShowEffectTime` | `250` | Time of the slide show transition effect in milliseconds, only used in full screen mode |

## Confirmation Options

| Key | Default | Description |
|---|---|---|
| `OverwriteOriginalFileWithoutSaveDialog` | `false` | If set to true, Ctrl-S overwrites the original file on disk, applying the current processings without showing a dialog or prompting the user to confirm. CAUTION: Use at your own risk! Be aware that the original image file is overwritten and cannot be restored! |
| `CropWithoutPromptLosslessJPEG` | `false` | If set to true, lossless JPEG transformations will crop the image as needed without prompting the user. This will remove 15 pixel rows/columns at the image borders in worst case. CAUTION: Use at your own risk! Be aware that the original image file is overwritten and the cropped borders cannot be restored! |
| `DeleteConfirmation` | `OnlyWhenNoRecycleBin` | Type of confirmation required when deleting a file with the 'x' button on the navigation panel. Possible values: Never, OnlyWhenNoRecycleBin, Always. OnlyWhenNoRecycleBin means to only require a confirmation when there is no recycle bin available (e.g. on memory sticks). Notice: Does not apply to deleting files with the Del key. To change this edit the KeyMap.txt and assign another command to the Del key. |

## File Save Options

| Key | Default | Description |
|---|---|---|
| `DefaultSaveFormat` | `jpg` | Default format for saving files. Supported formats: jpg, bmp, png, tif, webp |
| `JPEGSaveQuality` | `85` | Quality when saving JPEG files (in 0..100 where 100 is the highest quality) |
| `WEBPSaveQuality` | `85` | Quality when saving WEBP files with lossy compression (in 0..100 where 100 is the highest quality) |
| `CreateParamDBEntryOnSave` | `true` | Set to true to create a parameter DB entry when saving an image with Lucid Viewer to avoid processing it again |

## Print Options

| Key | Default | Description |
|---|---|---|
| `PrintMargin` | `1.0` | Default print margin (all sides) in centimeters |
| `PrintWidth` | `-15.0` | Default print width in centimeters. Use negative numbers to set the 'Fit to paper' printing mode. |
| `Units` | `auto` | Units (centimeters, inches) used, one of 'auto', 'metric' or 'english'. 'auto' uses the system setting. 'metric' uses the metric system, e.g. centimeters for length. 'english' uses the English/US system, e.g. inches for length |

## Custom User Commands

| Key | Default | Description |
|---|---|---|
| `UserCmd0` | `"KeyCode: Shift+Del Cmd: 'cmd /c del %filename%' Confirm: 'Do you really want to permanently delete the file %filename%?' HelpText: 'Shift+Del\tDelete current file on disk permanently' Flags: 'WaitForTerminate ReloadFileList MoveToNext'"` |  |

*This page is generated from `src/LucidViewer/Config/LucidViewer.ini` by `tools/wiki/gen_reference.py`.*
