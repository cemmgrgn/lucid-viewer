Every text of Lucid Viewer, including the names and descriptions in **Tools → Settings** and the names of the metadata
fields, comes from one file per language next to `LucidViewer.exe`: `strings_<language>.txt`, for example
`strings_ru.txt`. Turkish, German, French and Spanish are complete. The other languages come from JPEGView and miss the
newer texts, which then appear in English.

## File format

- UTF-8 text, one text per line: the English text, a **Tab**, the translation.
- Lines starting with `//` are comments. A line without a translation (nothing after the Tab) is ignored.
- The English text must be copied exactly, including punctuation and placeholders such as `%d`, `%s`, `%filename%`.
- Keep INI key names (`StoreToEXEPath`), values (`'auto'`, `true`), file names and command names untranslated.

## Updating a translation

1. Take `src/LucidViewer/Config/strings.txt` from the repository. It lists every English text, the newest at the end.
2. Copy the lines missing in your `strings_<language>.txt` and add the translations.
3. To see how many texts are missing in each language, run `python tools/i18n/settings_strings.py`.
4. Test it: put the file next to `LucidViewer.exe` (or into the installation folder) and choose the language in
   **Tools → Settings → User interface options → Language**, or set `Language=<code>` in the INI file.
5. Send it as a pull request or attach it to an issue.

If a translated text does not fit somewhere, please open an issue with a screenshot.

## How the settings texts work

The settings window reads the global `LucidViewer.ini`. The name of a setting is made from its key (`ShowToolbar` →
`Show Toolbar`), the description is the comment above the key joined into one line, and for lists the text after each
value (`0: open embedded thumbnail`) is translated separately. `tools/i18n/settings_strings.py --update` adds these texts to
`strings.txt` when the INI file changes.
