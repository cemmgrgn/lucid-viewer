# Contributing

Thanks for helping to make Lucid Viewer better.

## Reporting a bug

Open an [issue](https://github.com/cemmgrgn/lucid-viewer/issues) with

- what you did, what you expected and what happened,
- the Lucid Viewer version (Help → About) and your Windows version,
- the image file if the problem happens with one file only (or a link to it).

## Pull requests

- Build instructions are in [docs/BUILDING.md](docs/BUILDING.md).
- Keep changes focused, one feature or fix per pull request.
- Startup speed matters: if a change runs at startup, measure it with `tools/bench/startup_bench.py`
  or `LUCID_TRACE=1` before and after, and mention the numbers.
- New settings go into `src/LucidViewer/Config/LucidViewer.ini` (and the `.tpl`) with a comment above the key,
  the settings window reads its descriptions from there.
- New user-visible text goes through `CNLS::GetString(...)`. Add the English text to `Config/strings.txt` and,
  if you can, the translation to `Config/strings_<language>.txt`.

## Translations

The string tables are in `src/LucidViewer/Config/strings_<language>.txt`, one `English<TAB>Translation` pair per line.
Many lines added by Lucid Viewer are not translated yet in most languages, help is welcome.

## License

By contributing you agree that your contribution is licensed under the GNU General Public License v2 or later.
