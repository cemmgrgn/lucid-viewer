# Changelog

## Unreleased

### New
- **Thumbnails of the folder** (<kbd>G</kbd>, toolbar button): a grid of all images of the folder, a search result or
  dropped files. Select several with <kbd>Ctrl</kbd>/<kbd>Shift</kbd> + click, <kbd>Shift</kbd> + arrow keys or
  <kbd>Ctrl</kbd>+<kbd>A</kbd>, then rate them (<kbd>0</kbd>–<kbd>5</kbd>), add or remove tags, copy them
  (<kbd>Ctrl</kbd>+<kbd>C</kbd>, to paste in Explorer), copy or move them to a folder, rename them (<kbd>F2</kbd>),
  convert them or move them to the recycle bin (<kbd>Del</kbd>). Enter or a double click opens the image.
- **Timeline** (<kbd>Shift</kbd>+<kbd>G</kbd>): the thumbnails sorted by the date taken, grouped by month.
- **Compare** two selected images side by side in a window of their own, zoom and move both at once.
- **Batch rename** with a pattern: `{name}`, `{n}` (number), `{date}` and `{time}` (taken), with a preview.
- **Search photos** (<kbd>Ctrl</kbd>+<kbd>F</kbd>) in a folder and its sub folders by words of the name, `tag:`, `stars:`,
  `from:` and `to:` (date taken); the results are shown as thumbnails and can be browsed like a folder.
- **Copy file** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>C</kbd>): the file itself goes to the clipboard and can be pasted in
  Explorer, a mail or a chat.
- **Copy to folder** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>) and **Move to folder** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>X</kbd>)
  in the File and context menu. The last folder is suggested (also after a restart), a move shows the next photo and
  can be undone.
- **File properties** (<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>Enter</kbd>): the properties window of Explorer.
- **Open with another app** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>): the *Open with* window of Windows.
- After removing the background, <kbd>Ctrl</kbd>+<kbd>C</kbd> copies the whole image **with transparency** (PNG on the
  clipboard, for Word, PowerPoint, browsers and chats; on white for other programs).
- Pinned and recent photos on the start screen follow a file that is moved or renamed in Lucid Viewer.
- **Tags** (digiKam-like): <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd> edits the tags of a photo, they are written into
  the file as Windows keywords (Explorer, digiKam, Lightroom read them), undo works. <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd>
  shows only the photos of the folder with a tag, the status bar shows the active filter.
- **Sort by date taken** (EXIF, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>T</kbd>, also `FileDisplayOrder=DateTaken`), oldest first.
- **Show on map** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>L</kbd>): the GPS location of the photo on OpenStreetMap.
- **Go to image number** (<kbd>Ctrl</kbd>+<kbd>G</kbd>), **Duplicate file** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>D</kbd>),
  **Copy file name** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>N</kbd>), **Copy as data URI (base64)** for HTML, CSS and Markdown.
- **Pick color** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>P</kbd>): the next click copies the color of the pixel as `#RRGGBB`.
- **Set as lock screen** in the wallpaper menu.
- Lossless 180° rotation on <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd>.
- Pinch zoom with two fingers on touch screens.
- Several dropped files are a list of their own, the arrow keys go through them.
- `OpenLastImage` setting: started without an image, the photo viewed last is opened instead of the start screen.
- **Open in new window** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>N</kbd>, command line `/newwindow`): a second window, also
  in single instance mode, to compare two photos side by side.
- **Help → Check for updates**: compares the version with the latest release on GitHub and opens the download page.
  Only on request, the program never checks by itself.

### Fixed
- Possible crash when closing the window: the start screen thumbnails and drawings were deleted after GDI+ was shut down.
- Open in the existing window (*SingleInstance*): the window was not found while it showed the start screen, a window
  of another program with "Lucid Viewer" in its title could be taken for it, and the window was not brought to the front
  or restored when minimized. Starting the program again without a file now only brings the window to the front.
- Explorer *Convert to* with many files: files handed over at the very end of a conversion were not converted.
- Deleting a file with a path longer than 260 characters could overwrite memory.
- *Set as default viewer* did nothing on Windows 10 and 11, it now opens the default apps page of the Windows settings
  (on Windows 11 the page of Lucid Viewer) and does not register the program a second time after an all-users setup.
- Closing the window with a slow network folder in the thumbnail strip could crash.
- The background removal model downloaded by the program is checked (SHA-256, as the setup does). A broken download
  was kept before and background removal failed until the file was deleted by hand.
- Dropping a file with a long path opened a cut path, a dropped item that does not exist was taken for a folder,
  and the drop was not released while a panel was open. Folders longer than 260 characters were not watched for changes.
- Two screenshots in the same second had the same file name, the second one overwrote the first.
- Changing permissions of a protected file association kept only the allowed entries of the original list.

### Old JPEGView parts and leftovers removed
- A key map taken over from JPEGView now gets the default keys of all Lucid Viewer commands (toolbar, side panel,
  edit, compare, draw, OCR, metadata...) that it does not name and whose key is free; before, only the newest
  commands got them. `IDM_UNDO` is no longer mistaken for `IDM_UNDO_TRANSPARENCY` when checking a user key map.
- Leftovers of the versions before the rename are removed: the autostart value *JPEGView Screenshot* (it started the tray
  tool again although it was switched off), the old tray tool that held the Print Screen key, and the Explorer verb
  *JPEGView.Convert* that showed a second *Convert to* menu. The setup removes them as well and also waits for the old
  tray tool.
- The uninstaller removes the registrations the program writes for the user (Open with, default apps).
- Temporary files of metadata changes are named `.lucidviewer-tmp`, the program and its code no longer use the JPEGView
  names (`AppDataPath`, `RegisterApp`, `APP_TITLE`...). The library build scripts write to `src\LucidViewer` again, the
  dead branch for the removed VS2017 project is gone.
- `KeyMap-README.html` lists all commands of Lucid Viewer again, the license line of the string tables points to this
  repository.

### Changed
- WICLoader.dll and the installer carry the product name *Lucid Viewer* and the same version as the program (one
  version in `src/Version.h`), as code signing requires.

### Project
- winget package `CemGirgin.LucidViewer` (`packaging/winget`), new versions are sent to winget-pkgs when a release is
  published.
- Wiki with installation, feature pages, keyboard shortcuts, settings and command line reference (`docs/wiki`).
- SignPath configuration for signed releases (`.signpath`).

## 1.0.0 (2026-11-05)

First release of Lucid Viewer, based on JPEGView 1.3.46 (with the updated codecs by qbnu).

### New
- Modern window: toolbar, status bar, filmstrip and a side panel with Information, Edit and Draw tabs; dark and light theme.
- Start screen with recent, pinned and screenshot photos.
- Editing: exposure, contrast, temperature, tint, vignette and more sliders with an Auto button, before/after comparison, crop with aspect ratios, straighten, resize; undo and redo for every change.
- Drawing tools, text, pixelate and a reusable signature.
- Copy text from images (OCR) with word selection.
- AI background removal on the device (U²-Net, downloaded on request) and color removal with transparent PNG output.
- Screenshot tool with region and window selection, markup, magnifier and OCR (Print Screen).
- Convert to JPG, PNG, WebP, HEIC, PDF and more from Explorer's right-click menu, the command line or a whole folder.
- Star ratings with filter and sort, metadata view and editor, remove GPS location (also PNG and WebP).
- Similar photo finder, export with size estimate, Windows Share, drag photos out of the window.
- Searchable settings window and keyboard shortcut editor.
- Installer with optional AI model download, preferences and appearance pages; portable zip.

### Faster
- 24 MP JPEG on screen in ~140 ms instead of ~345 ms: the star rating is read in the background, large photos show a fast first frame, rarely used DLLs are loaded on demand.

### Changed
- Settings are stored in `%APPDATA%\LucidViewer`. The settings of JPEGView are copied on the first start.

The history of JPEGView is in [docs/history/JPEGView-CHANGELOG.txt](docs/history/JPEGView-CHANGELOG.txt).
