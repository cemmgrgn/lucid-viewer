Lucid Viewer can convert photos to another format, make them smaller or remove their metadata. You can do it from Explorer, from the program or from the command line.

## From Explorer

Right-click one or more images in Explorer and choose **Convert to**. Pick a format, such as JPG, PNG, WebP, HEIC or PDF.

The **Convert to** menu is installed when you choose the option during setup. You can also run this command after you change the custom presets:

```
LucidViewer.exe /registerconvertmenu
```

To remove the menu, run `LucidViewer.exe /unregisterconvertmenu`.

## From the program

- Press <kbd>Ctrl</kbd>+<kbd>D</kbd> or use **Convert to** in the menu.
- To convert a whole folder, use **Tools > Convert all images of this folder** (images in sub folders are not included).

## From the command line

```
LucidViewer.exe /convert:<preset> <file or folder>
```

A preset is either a format, or an operation and a format, separated by a colon:

```
format
operation:format
```

Example:

```
LucidViewer.exe /convert:fit1920:jpg "C:\Photos\IMG_0001.HEIC"
```

This makes a JPG with a long edge of at most 1920 pixels.

Add `/silent` for scripts: no message is shown, the exit code is `0` when all files were converted, `1` when a file
could not be converted and `2` for an unknown preset.

## Formats

| Format | Notes |
|---|---|
| `jpg` | Small files, lossy |
| `png` | Lossless, supports transparency |
| `webp` | Small files, lossy |
| `webpll` | WebP, lossless |
| `bmp` | Uncompressed |
| `tiff` | Print and archive use |
| `gif` | Limited colors |
| `ico` | Icon files |
| `pdf` | One PDF with the image |
| `heic` | Needs the HEVC extension to save. See [[FAQ|FAQ]]. |
| `same` | Keeps the format of the source file. Needs an operation, for example `scale50:same`. |

AVIF cannot be saved.

## Operations

| Operation | Meaning | Example |
|---|---|---|
| `scaleN` | Resize to N percent, from 1 to 1000 | `scale50` |
| `fitN` | Set the long edge to at most N pixels | `fit1920` |
| `rotl` | Rotate left | `rotl` |
| `rotr` | Rotate right | `rotr` |
| `rot180` | Rotate 180 degrees | `rot180` |
| `nometa` | Remove metadata, including EXIF and GPS | `nometa` |

Operations and formats can be combined with a colon, for example `scale30:jpg` or `nometa:webp`.

## Where the files go

The converted file is saved next to the source file, with the new extension. For example `photo.heic` becomes `photo.jpg`.

- If that name exists, the program adds a number: `photo (1).jpg`, then `photo (2).jpg`, and so on.
- Set `ConvertOutputFolder` to save all converted files in one folder. A relative name such as `converted` makes a subfolder of the source folder. An absolute path is used for all files.

## Custom menu entries

Add your own presets to the **Convert to** menu with the `ConvertCustomPresets` setting. Separate the entries with semicolons:

```
ConvertCustomPresets=fit2560:webp;scale30:jpg
```

After you change this setting, run `LucidViewer.exe /registerconvertmenu` again to update the Explorer menu.

See the [[Settings reference|Settings-reference]] for the other conversion settings.
