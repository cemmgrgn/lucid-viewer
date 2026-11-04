## Print Screen opens the Snipping Tool, not Lucid Viewer

Windows 11 gives the Print Screen key to the Snipping Tool. Turn this off in **Settings > Accessibility > Keyboard** with **Use the Print screen key to open screen capture**. See [[Screenshot tool|Screenshot-tool]].

## Windows shows a SmartScreen warning

Releases are not code signed yet. SmartScreen warns about files that it does not know. Download Lucid Viewer only from the [release page](https://github.com/cemmgrgn/lucid-viewer/releases). Code signing is being set up. See [[Code signing and privacy|Code-signing-and-privacy]].

## Where are the settings?

| File | Contents |
|---|---|
| `%APPDATA%\LucidViewer\LucidViewer.ini` | Your settings. Only the changed values are written here. |
| `%APPDATA%\LucidViewer\KeyMap.txt` | Your keyboard shortcuts. |

You can change the settings in **Tools > Settings**. The file is for people who prefer a text editor. See the [[Settings reference|Settings-reference]].

The portable version keeps these files next to `LucidViewer.exe`.

## Where are the shortcuts?

Change them in **Help > Keyboard shortcuts**. The shortcut editor writes `KeyMap.txt`. See [[Keyboard shortcuts|Keyboard-shortcuts]].

## HEIC files open, but saving HEIC does not work

Viewing HEIC and HEIF files needs nothing extra. The decoder is included with Lucid Viewer.

Saving HEIC uses the Windows HEIF encoder. This needs the **HEVC Video Extensions** from the Microsoft Store. Install them, then try again. You can also convert the photo to JPG or PNG instead. See [[Converting images|Converting-images]].

## AVIF cannot be saved

AVIF files open, but Lucid Viewer cannot save them. Convert the photo to JPG, PNG or WebP instead.

## My JPEGView settings are gone

Nothing is removed. On the first start, Lucid Viewer copies your JPEGView settings, shortcuts and signature to `%APPDATA%\LucidViewer`. The JPEGView folder is not changed. See [[Installation|Installation]].

## Portable or installed: which one should I use?

- **Installed** is the best choice for most people. It adds shortcuts and Windows integration, and it stores the settings in your user folder.
- **Portable** is a good choice for a USB stick or for a PC where you cannot install programs. The settings stay next to the program.

## The text copy (OCR) does not find any text

- Check that the language of the text is installed in Windows.
- Set `OcrLanguage` to the language of the text. See [[OCR|OCR]].

## How do I report a bug?

Open an [issue on GitHub](https://github.com/cemmgrgn/lucid-viewer/issues). Include:

- what you did, what you expected and what happened
- the Lucid Viewer version (**Help > About**) and your Windows version
- the image file, if the problem happens with one file only, or a link to it
