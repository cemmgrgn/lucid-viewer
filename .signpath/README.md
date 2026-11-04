# SignPath setup (maintainers)

Release builds are signed through [SignPath Foundation](https://signpath.org) once the project is accepted.
The public policy is [docs/CODE_SIGNING.md](../docs/CODE_SIGNING.md). This folder keeps the SignPath configuration
in the repository so changes to it are reviewed like code.

| File | Paste it into SignPath as |
|---|---|
| `artifact-configurations/program.xml` | artifact configuration `program` (LucidViewer.exe and WICLoader.dll) |
| `artifact-configurations/installer.xml` | artifact configuration `installer` (the Inno Setup installer) |
| `policies/lucid-viewer/release-signing.yml` | pipeline policy, external definition, for the signing policy `release-signing` |

## Checklist

1. **Apply** at <https://signpath.org/apply> with the answers below.
2. **MFA**: two-factor authentication on GitHub (already required for SignPath) and on the SignPath account.
3. **GitHub ruleset** for the pipeline policy (blocks force pushes and deletion of `main` and of release tags):
   ```
   gh api -X POST repos/cemmgrgn/lucid-viewer/rulesets --input .github/rulesets/main.json
   gh api -X POST repos/cemmgrgn/lucid-viewer/rulesets --input .github/rulesets/release-tags.json
   ```
4. After approval, in SignPath:
   - add the trusted build system **GitHub.com** to the organization and link it to the project, install the SignPath
     GitHub App for this repository;
   - project slug `lucid-viewer`, signing policy `release-signing` (approver: Cem Girgin, manual approval);
   - artifact configurations `program` and `installer` from this folder;
   - pipeline policy from `policies/lucid-viewer/release-signing.yml`, level *Log* first, *Enforce* after the first
     signing request passes;
   - an API token for a CI user with the *submitter* role.
5. In GitHub (Settings → Secrets and variables → Actions): secret `SIGNPATH_API_TOKEN`, variable
   `SIGNPATH_ORGANIZATION_ID`. From now on the Build workflow sends every `v*` tag for signing and waits up to an hour for
   the approval (then the job fails and can be re-run).
6. Remove the *Status* line from docs/CODE_SIGNING.md and the SmartScreen notes in the wiki (Installation, FAQ,
   Code signing and privacy) once the first signed release is out.

Note on the installer: Inno Setup pads the strings of the installer's version resource with spaces
(`"Lucid Viewer"` is stored as 60 characters, `"1.0.0.0"` as 50). The program files are not padded. If the first
signing request of the installer fails on the metadata restrictions, ask SignPath support how to match padded values
before loosening `installer.xml`.

## Application answers

- **Project name:** Lucid Viewer
- **Repository:** https://github.com/cemmgrgn/lucid-viewer
- **Homepage / download page:** https://cemmgrgn.github.io/lucid-viewer/ ,
  https://github.com/cemmgrgn/lucid-viewer/releases
- **Code signing policy:** https://github.com/cemmgrgn/lucid-viewer/blob/main/docs/CODE_SIGNING.md
- **License:** GPL-2.0-or-later (all bundled libraries are open source, see THIRD_PARTY_NOTICES.md)
- **Description:** A fast image viewer and light editor for Windows 10 and 11. It opens JPEG, PNG, HEIC, AVIF, JPEG XL,
  WebP and RAW photos and adds editing sliders with before/after comparison, crop and straighten, drawing and
  signatures, OCR with Windows' own text recognition, on-device AI background removal, a screenshot tool, conversion
  from Explorer's context menu, star ratings and a metadata editor.
- **Relation to JPEGView:** Lucid Viewer started from JPEGView 1.3.46 (GPL v2, the sylikc fork, which publishes unsigned
  builds) and is now a separate product: own name, icon, settings folder, installer and release line (1.0.0, November
  2026). About 10,000 lines of new code in new modules (window frame and side panel, start screen, screenshot tool,
  converter, drawing tools, metadata editor, OCR, settings and shortcut editors), plus changes throughout the old code.
  The JPEGView authors are credited in the README, the About dialog and the version resource.
- **What is signed:** LucidViewer.exe, WICLoader.dll (both built from this repository) and the Inno Setup installer.
  The upstream image libraries (libheif, libde265, dav1d, libavif, libjxl, Brotli, LibRaw, Little CMS) are shipped
  unsigned and are not submitted.
- **Build:** GitHub Actions on GitHub-hosted runners (`.github/workflows/build.yml`), triggered by version tags; the
  unsigned files are uploaded as workflow artifacts and submitted with `signpath/github-action-submit-signing-request`.
- **Network access:** only the download of the background removal model from GitHub, when the user asks for it or
  selects it in setup.
- **Team:** Cem Girgin (@cemmgrgn): author, reviewer and approver.
