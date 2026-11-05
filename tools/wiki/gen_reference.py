"""Generates the reference pages of the GitHub wiki from the program's own files.

    python tools/wiki/gen_reference.py docs/wiki

Keyboard-shortcuts.md: default key map (Config/KeyMap.txt.default) with the readable command names the
shortcut editor shows (context menu texts of LucidViewer.rc, overridden by s_commandTexts in SettingsEditor.cpp)
and the same categories as the editor.
Settings-reference.md: every key of the global Config/LucidViewer.ini with its default and its comment.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "LucidViewer"

CATEGORIES = ["File", "Navigation", "View and zoom", "Editing", "Drawing", "Tools", "Slideshow", "Other"]


def category(name):
    """Same rules as GetCommandCategory in SettingsEditor.cpp."""
    has = lambda *parts: any(p in name for p in parts)
    if has("ANNOTATION", "SIGNATURE", "DRAWPANEL", "TRANSPAREN", "REMOVE_BACKGROUND"):
        return 4
    if has("SLIDESHOW", "MOVIE", "EFFECT"):
        return 6
    if has("PARAM_DB"):
        return 3
    if has("SCREENSHOT", "SETTINGS", "SHORTCUTS", "SIMILAR", "CONFIG", "HELP", "ABOUT", "USER_CMD", "OCR",
           "EXPLORER_CONVERT", "SAVE_PARAMETERS", "OPENWITH", "UPDATES", "PICK_COLOR"):
        return 5
    if has("OPEN", "SAVE", "RELOAD", "PRINT", "COPY", "PASTE", "EXPLORE", "BATCH", "RENAME", "RECYCLE", "DELETE",
           "TOUCH", "WALLPAPER", "EXPORT", "SHARE", "CONVERT", "EXIT", "METADATA", "TO_FOLDER", "PROPERTIES", "DUPLICATE", "FILE_NAME",
           "BASE64", "LOCK_SCREEN", "EDIT_TAGS", "ON_MAP"):
        return 0
    if has("NEXT", "PREV", "FIRST", "LAST", "LOOP", "SORT", "RATING", "GOTO", "FILTER_BY_TAG") or name == "IDM_TOGGLE":
        return 1
    if has("ZOOM", "FIT", "FILL", "SPAN", "FULL_SCREEN", "SHOW_", "TOGGLE_", "HIDE", "ALWAYS_ON_TOP", "PAN_",
           "MINIMIZE", "MONITOR"):
        return 2
    if has("CONTRAST", "GAMMA", "SHARPEN", "COLOR", "LDC", "AUTO_", "LANDSCAPE", "UNSHARP", "RESET", "KEEP",
           "ROTATE", "MIRROR", "CROP", "CHANGESIZE", "PERSPECTIVE", "COMPARE", "EDITPANEL", "UNDO", "REDO",
           "LOSSLESS", "RESAMPLING", "SHADOWS", "HIGHLIGHTS"):
        return 3
    return 7


def menu_labels():
    """IDM name -> 'Sub menu: item' text from the PopupMenu resource."""
    text = (SRC / "LucidViewer.rc").read_text(encoding="utf-8", errors="replace")
    start = text.index("POPUPMENU MENU")
    labels, stack, depth = {}, [], 0
    for line in text[start:].splitlines()[1:]:
        s = line.strip()
        if s == "BEGIN":
            depth += 1
            continue
        if s == "END":
            depth -= 1
            if stack and len(stack) > depth - 1:
                stack.pop()
            if depth == 0:
                break
            continue
        m = re.match(r'POPUP\s+"([^"]*)"', s)
        if m:
            stack.append(m.group(1).replace("&", ""))
            continue
        m = re.match(r'MENUITEM\s+"([^"]*)"\s*,\s*(\w+)', s)
        if m:
            label = m.group(1).split("\\t")[0].strip().replace("&", "")
            parents = stack[1:]  # the outer "PopupMenu" is not a prefix
            if parents and label:
                label = parents[-1] + ": " + label
            labels.setdefault(m.group(2), label)
    return labels


def command_texts():
    text = (SRC / "SettingsEditor.cpp").read_text(encoding="utf-8", errors="replace")
    block = text[text.index("s_commandTexts[] = {"):]
    block = block[:block.index("};")]
    return dict(re.findall(r'\{\s*_T\("(IDM_\w+)"\)\s*,\s*_T\("((?:[^"\\]|\\.)*)"\)\s*\}', block))


def mouse_actions():
    text = (SRC / "SettingsEditor.cpp").read_text(encoding="utf-8", errors="replace")
    block = text[text.index("s_mouseActions[] = {"):]
    block = block[:block.index("};")]
    return re.findall(r'\{\s*_T\("([^"]*)"\)\s*,\s*_T\("([^"]*)"\)\s*\}', block)


def md(s):
    return s.replace("|", "\\|").replace("<", "&lt;")


def key_md(key):
    return " + ".join(f"<kbd>{md(k)}</kbd>" for k in key.split("+"))


def shortcuts_page():
    labels = menu_labels()
    labels.update(command_texts())
    groups = {i: [] for i in range(len(CATEGORIES))}
    for line in (SRC / "Config" / "KeyMap.txt.default").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        key, cmd = parts[0], parts[1]
        label = labels.get(cmd) or cmd.replace("IDM_", "").replace("_", " ").capitalize()
        groups[category(cmd)].append((key, label, cmd))
    out = [
           "These are the default keys. Change them in **Help → Keyboard shortcuts** (also reachable from "
           "**Tools → Settings**): pick a command, press the new key, save. The page warns when a key is already used.",
           ""]
    for i, name in enumerate(CATEGORIES):
        if not groups[i]:
            continue
        out += [f"## {name}", "", "| Key | Action |", "|---|---|"]
        out += [f"| {key_md(k)} | {md(l)} |" for k, l, _ in groups[i]]
        out.append("")
    out += ["## Mouse", "", "| Action | What it does |", "|---|---|"]
    out += [f"| {md(a)} | {md(t)} |" for a, t in mouse_actions()]
    out += ["",
            "## Editing the key map by hand", "",
            "The shortcut editor writes `%APPDATA%\\LucidViewer\\KeyMap.txt`. You can also edit that file in a text editor: "
            "one binding per line, the key (with `Ctrl+`, `Alt+`, `Shift+` modifiers) followed by the command name, "
            "for example", "", "```", "Ctrl+Shift+S            IDM_SAVE_SCREEN", "```", "",
            "Lines starting with `//` are comments. The command names are listed in `symbols.km` next to the program, "
            "and `KeyMap-README.html` there explains the syntax in detail. New commands added in later versions get "
            "their default key automatically unless your file already uses that key.", "",
            "*This page is generated from the source by `tools/wiki/gen_reference.py`.*", ""]
    return "\n".join(out)


def settings_page():
    lines = (SRC / "Config" / "LucidViewer.ini").read_text(encoding="utf-8").splitlines()
    out = [
           "Most settings are in **Tools → Settings**, a searchable window that explains each option and applies "
           "changes right away. This page lists every key of the settings file for those who prefer a text editor.", "",
           "- **Global defaults:** `LucidViewer.ini` next to `LucidViewer.exe` (do not edit it in *Program Files*, "
           "it is replaced by updates).",
           "- **Your settings:** `%APPDATA%\\LucidViewer\\LucidViewer.ini`, section `[LucidViewer]`. Only the keys you "
           "change are written there; everything else comes from the global file.",
           "- **Portable:** with `StoreToEXEPath=true` (set in the portable zip) settings stay next to the program.", ""]
    comment, section, rows, prev_key = [], None, [], None

    def flush():
        if rows:
            out.extend([f"## {section or 'General'}", "", "| Key | Default | Description |", "|---|---|---|"])
            out.extend(rows)
            out.append("")
            rows.clear()

    i = 0
    while i < len(lines):
        s = lines[i].strip()
        # section banners:  ; ****  / ; * TITLE / ; *
        if s.startswith("; ****"):
            j = i + 1
            title = None
            while j < len(lines) and not lines[j].strip().startswith("; ****"):
                t = lines[j].strip().lstrip(";").strip().lstrip("*").strip()
                if t and title is None:
                    title = t
                j += 1
            if title:
                flush()
                section = title.title()
            comment = []
            i = j + 1
            continue
        if s.startswith(";"):
            comment.append(s.lstrip(";").strip())
            prev_key = None
        elif "=" in s and not s.startswith("["):
            key, value = s.split("=", 1)
            parts = [c for c in comment if c and not set(c) <= set("=*-")]
            # comment lines are sentences without a final stop now and then
            desc = " ".join(p if p[-1] in ".:!?,;(" or k + 1 == len(parts) or parts[k + 1][:1].islower() else p + "."
                            for k, p in enumerate(parts))
            if not desc and prev_key:
                desc = f"Like `{prev_key}` above."
            elif desc:
                prev_key = key.strip()  # the key whose comment the following keys share
            rows.append(f"| `{md(key.strip())}` | `{md(value.strip())}` | {md(desc)} |" if value.strip()
                        else f"| `{md(key.strip())}` | *(empty)* | {md(desc)} |")
            comment = []
        elif not s:
            comment = []
            prev_key = None
        i += 1
    flush()
    out += ["*This page is generated from `src/LucidViewer/Config/LucidViewer.ini` by `tools/wiki/gen_reference.py`.*", ""]
    return "\n".join(out)


if __name__ == "__main__":
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    (target / "Keyboard-shortcuts.md").write_text(shortcuts_page(), encoding="utf-8")
    (target / "Settings-reference.md").write_text(settings_page(), encoding="utf-8")
    print("written to", target)
