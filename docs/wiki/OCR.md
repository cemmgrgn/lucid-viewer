OCR (optical character recognition) finds text in an image and lets you copy it. Lucid Viewer uses the text recognition that is built into Windows (`Windows.Media.Ocr`).

![Every recognized word is framed, two lines are selected for copying](https://raw.githubusercontent.com/cemmgrgn/lucid-viewer/main/docs/images/ocr.webp)

## Copy text from a photo

1. Open the image in Lucid Viewer.
2. Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> (or the **T** button of the toolbar). Every recognized word is
   framed on the image.
3. Drag over the words you need: they are copied when you release the mouse button. A double click copies a whole line,
   <kbd>Ctrl</kbd>+<kbd>A</kbd> selects all text and <kbd>Ctrl</kbd>+<kbd>C</kbd> copies the selection.
4. <kbd>Esc</kbd> ends the selection. Pressing <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>T</kbd> again copies the selection
   (or all text) and ends it.

## Text on the screen

For text outside Lucid Viewer use the screenshot tool: press <kbd>PrtScn</kbd>, select the region, then select the words
with the mouse and press <kbd>Ctrl</kbd>+<kbd>C</kbd>.
You can also copy all the text of the region with <kbd>Shift</kbd>+<kbd>PrtScn</kbd>. See [[Screenshot tool|Screenshot-tool]].

## Language

Lucid Viewer chooses the language in this order:

1. The language set in `OcrLanguage`, for example `tr` or `en-US`.
2. If it is empty, the language of a keyboard layout that is not English.
3. If there is no such layout, the Windows display language.

The language must be installed in Windows. To install more OCR languages, use the Windows language settings.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `OcrLanguage` | empty | Language for text recognition. The language must be installed in Windows. |

See the [[Settings reference|Settings-reference]] for more.
