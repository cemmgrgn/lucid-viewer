# Changelog

## Unreleased

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
