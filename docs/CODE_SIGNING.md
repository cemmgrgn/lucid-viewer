# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## What is signed

Release builds of `LucidViewer.exe`, `WICLoader.dll` and the installer `LucidViewer-<version>-Setup-x64.exe`.
They are built from this repository by the [Build workflow](../.github/workflows/build.yml) on GitHub Actions when a
version tag (`v*`) is pushed, and signed only after a manual approval.

The image libraries in `src/LucidViewer/lib*` (libheif, libde265, dav1d, libavif, libjxl, Brotli, LibRaw, Little CMS) are
upstream open source binaries. They are shipped as they are and are not signed with the Lucid Viewer certificate.
Their licenses are listed in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Team

| Role | Members |
|---|---|
| Committers and reviewers | [Cem Girgin (@cemmgrgn)](https://github.com/cemmgrgn) |
| Approvers | [Cem Girgin (@cemmgrgn)](https://github.com/cemmgrgn) |

Changes from other contributors are reviewed by a committer before they are merged. All team members use multi-factor
authentication for GitHub and SignPath.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user
or the person installing or operating it.

The only network access is the download of the background removal model (`u2net.onnx` from GitHub), which happens when
the user chooses *Remove background* for the first time or selects the model during setup. Sharing a photo uses the
Windows share window and goes only where the user sends it.

## Installation and removal

The installer shows every change it makes (shortcuts, Explorer menu, file associations, start with Windows) as an option
on its pages. *Settings → Apps* removes everything again; on removal the user is asked whether to keep the settings.
