"""English texts of the settings window and other new texts, and their place in the string tables.

    python tools/i18n/settings_strings.py            lists the texts missing in each strings_xx.txt
    python tools/i18n/settings_strings.py --update   adds the missing texts to the reference file strings.txt

The settings window (SettingsEditor.cpp) shows for every key of the global LucidViewer.ini a name made from the key
("ShowToolbar" -> "Show Toolbar"), the comment above the key as description (joined to one line with single spaces)
and for lists of values the text after each value ("0: open embedded thumbnail"). All of them are looked up in the
string table, so this script reads the INI file with the same rules as ReadDefinitions/ParseOptions in SettingsEditor.cpp.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "src" / "LucidViewer" / "Config"


def humanize(key):
    """Same as Humanize in SettingsEditor.cpp."""
    s = ""
    for i, c in enumerate(key):
        upper, digit = c.isupper(), c.isdigit()
        if i > 0 and (upper or digit):
            p = key[i - 1]
            next_lower = i + 1 < len(key) and key[i + 1].islower()
            if (upper and (p.islower() or p.isdigit() or (p.isupper() and next_lower))) or (digit and not p.isdigit()):
                s += " "
        s += c
    return s


def one_line(s):
    s = s.replace("\n", " ").replace("\t", " ")
    while "  " in s:
        s = s.replace("  ", " ")
    return s.strip()


def option_label(rest):
    """The translated part of OptionLabel in SettingsEditor.cpp, None if there is no text."""
    rest = rest.strip().lstrip(":").strip()
    if rest[:4].lower() == "for ":
        rest = rest[4:]
    elif rest[:3].lower() == "to ":
        rest = rest[3:]
    rest = rest.rstrip(".,;")
    return rest or None


def split_list(text):
    text = text.replace(" or ", ",").replace(" and ", ",").replace("'", "").replace('"', "")
    values = []
    for part in text.split(","):
        part = part.strip().rstrip(".")
        if part and " " not in part:
            values.append(part)
    return values


def parse_options(key, default, comment):
    """Labels of the values if the setting is shown as a list, as ParseOptions in SettingsEditor.cpp."""
    d = default.strip()
    if d.lower() in ("true", "false") or key == "Language":
        return []
    if key.endswith("Color") and re.match(r"^\s*-?\d+\s+-?\d+\s+-?\d+", d):
        return []
    options = []  # (value, text or None)

    def add(value, text=None):
        if not any(o[0].lower() == value.lower() for o in options):
            options.append((value, text))

    for line in [l for l in comment.split("\n") if l]:
        t = line.strip()
        colon = t.find(":")
        if colon > 0:
            value = t[:colon].strip()
            if value and all(ch.isdigit() or (k == 0 and ch == "-") for k, ch in enumerate(value)):
                options.append((value, option_label(t[colon + 1:])))
        if len(t) > 2 and t[0] == "'":
            end = t.find("'", 1)
            if end > 1 and " " not in t[1:end]:
                options.append((t[1:end], option_label(t[end + 1:])))
        o = t.find(" or ")
        if o > 0:
            a = o
            while a > 0 and (t[a - 1].isalnum() or t[a - 1] == "-"):
                a -= 1
            b = o + 4
            while b < len(t) and (t[b].isalnum() or t[b] == "-"):
                b += 1
            first, second = t[a:o], t[o + 4:b]
            if first and second and (first.lower() == d.lower() or second.lower() == d.lower()):
                add(first)
                add(second)
        for marker in ("can be ", "Possible values:", "one of "):
            pos = t.find(marker)
            if pos >= 0:
                lst = t[pos + len(marker):]
                end = lst.find(". ")
                if end > 0:
                    lst = lst[:end]
                values = split_list(lst)
                if len(values) >= 2:
                    for v in values:
                        add(v)
    if len(options) >= 2 and any(o[0].lower() == d.lower() for o in options):
        return [text for _, text in options if text]
    return []


def read_settings():
    """(key, name, description, option texts) of every key of the global INI file."""
    settings, keys = [], set()
    comment, previous = "", ""
    for line in (CONFIG / "LucidViewer.ini").read_text(encoding="utf-8-sig").splitlines():
        t = line.strip()
        separator = t.startswith("; ***") or t.startswith("; ---")
        if not t or t[0] == "[" or separator:
            comment = ""
        elif t.startswith("; * ") and previous.strip().startswith("; ***"):
            comment = ""
        elif t.startswith("; *"):
            pass
        elif t.startswith("; - ") and previous.strip().startswith("; ---"):
            comment = ""
        elif t[0] == ";":
            text = t[1:]
            if text.startswith(" "):
                text = text[1:]
            comment += text + "\n"
        else:
            eq = t.find("=")
            if eq > 0:
                key = t[:eq].strip()
                default = t[eq + 1:]
                c = comment.rstrip("\n")
                if all(ch.isalnum() or ch == "_" for ch in key) and key.lower() not in keys:
                    keys.add(key.lower())
                    description = c
                    if key == "Language":
                        cut = description.find("Lucid Viewer will")
                        if cut > 0:
                            description = description[:cut]
                    settings.append((key, humanize(key), one_line(description), parse_options(key, default, c)))
            comment = ""
        previous = line
    return settings


def wanted_texts():
    texts = []
    for _, name, description, options in read_settings():
        for text in [name, description] + options:
            if text and text not in texts:
                texts.append(text)
    return texts


def table_keys(path):
    keys = set()
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("//") or "\t" not in line:
            continue
        english, translated = line.split("\t", 1)
        if path.name == "strings.txt" or translated.strip():
            keys.add(english)
    return keys


def main():
    texts = wanted_texts()
    reference = CONFIG / "strings.txt"
    known = table_keys(reference)
    missing = [t for t in texts if t not in known]
    if "--update" in sys.argv:
        if missing:
            content = reference.read_text(encoding="utf-8-sig")
            if not content.endswith("\n"):
                content += "\r\n"
            content += "".join(t + "\t\r\n" for t in missing)
            reference.write_bytes(b"\xef\xbb\xbf" + content.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
        print(f"{len(missing)} texts added to strings.txt")
        return
    print(f"{len(texts)} texts of the settings window, {len(missing)} not in strings.txt")
    all_keys = [line.split("\t", 1)[0] for line in reference.read_text(encoding="utf-8-sig").splitlines()
                if not line.startswith("//") and "\t" in line]
    for path in sorted(CONFIG.glob("strings_*.txt")):
        have = table_keys(path)
        print(f"{path.name}: {sum(1 for k in all_keys if k not in have)} of {len(all_keys)} texts not translated")


if __name__ == "__main__":
    main()
