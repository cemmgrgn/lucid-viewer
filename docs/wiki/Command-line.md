Start Lucid Viewer with options to open a folder as a slideshow, convert files or start the screenshot tool. Put the path first and the options after it. Use quotes when a path has spaces.

```
LucidViewer.exe "C:\Photos" /slideshow10 /fullscreen
```

## Options

| Option | What it does | Example |
|---|---|---|
| `<file or folder>` | Opens the file, or the first image of the folder. | `LucidViewer.exe "C:\Photos"` |
| `/slideshow[N]` | Starts a slideshow. `N` is the number of seconds per image. The default is 5. | `LucidViewer.exe "C:\Photos" /slideshow10` |
| `/fullscreen` | Starts in full screen mode. | `LucidViewer.exe "C:\Photos" /fullscreen` |
| `/autoexit` | Closes Lucid Viewer when the slideshow ends. Press <kbd>Esc</kbd> during the slideshow to close it too. | `LucidViewer.exe "C:\Photos" /slideshow5 /autoexit` |
| `/monitor<N>` | Starts on monitor `N`. See `DisplayMonitor` in the settings. | `LucidViewer.exe "C:\Photos" /monitor2` |
| `/newwindow` | Opens a window of its own, also when *Open images in the window that is already open* (`SingleInstance`) is on. **File → Open in new window** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>N</kbd>) uses it. | `LucidViewer.exe "C:\Photos\a.jpg" /newwindow` |
| `/order<X>` | File order. `X` is `M` (modified date), `C` (created date), `N` (name), `Z` (random) or `S` (size). | `LucidViewer.exe "C:\Photos" /orderN` |
| `/effect <Name>` | Transition for the slideshow. See the list below. | `LucidViewer.exe "C:\Photos" /slideshow5 /effect SlideLR` |
| `/transitiontime<ms>` | Duration of the transition in milliseconds, from 100 to 5000. | `LucidViewer.exe "C:\Photos" /slideshow5 /transitiontime500` |
| `/convert:<preset>` | Converts a file or folder. See [[Converting images|Converting-images]]. | `LucidViewer.exe /convert:scale50:jpg "C:\Photos"` |
| `/capture` | Starts a screenshot of a region or window. | `LucidViewer.exe /capture` |
| `/capturetext` | Starts a screenshot that copies the text of a region. | `LucidViewer.exe /capturetext` |
| `/tray` | Runs the screenshot tool in the notification area. See [[Screenshot tool|Screenshot-tool]]. | `LucidViewer.exe /tray` |
| `/registerconvertmenu` | Adds **Convert to** to the Explorer right-click menu. | `LucidViewer.exe /registerconvertmenu` |
| `/unregisterconvertmenu` | Removes **Convert to** from the Explorer right-click menu. | `LucidViewer.exe /unregisterconvertmenu` |

The installer uses the last two options.

## Transition effects

Use one of these names with `/effect`:

- `None`
- `Blend`
- `SlideRL`, `SlideLR`, `SlideTB`, `SlideBT`
- `RollRL`, `RollLR`, `RollTB`, `RollBT`
- `ScrollRL`, `ScrollLR`, `ScrollTB`, `ScrollBT`

Transitions are only shown in full screen mode. In window mode they are ignored.

## Developer option

`/uitest` is a developer option. It is used to take screenshots of the dialogs for the documentation. You do not need it for normal use.

## See also

- [[Settings reference|Settings-reference]] for the settings that match these options.
- [[Keyboard shortcuts|Keyboard-shortcuts]] for keys you can use during a slideshow.
