# Code signing and privacy

This page is a summary of the [code signing policy](https://github.com/cemmgrgn/lucid-viewer/blob/main/docs/CODE_SIGNING.md).

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## Code signing status

Code signing is being set up. It is not active yet, so current releases are not signed. Windows may show a SmartScreen warning when you start the installer. See [[FAQ|FAQ]].

When signing is active, these files will be signed:

- `LucidViewer.exe`
- `WICLoader.dll`
- the installer, `LucidViewer-<version>-Setup-x64.exe`

Release builds come from this repository, built by GitHub Actions when a version tag is pushed. A person must approve each signing request.

The image libraries that ship with the program are upstream open source binaries. They are not signed with the Lucid Viewer certificate. Their licenses are listed in the `THIRD_PARTY_NOTICES.md` file of the repository.

## Team

| Role | Member |
|---|---|
| Authors | [Cem Girgin](https://github.com/cemmgrgn) |
| Reviewers | [Cem Girgin](https://github.com/cemmgrgn) |
| Approvers | [Cem Girgin](https://github.com/cemmgrgn) |

Changes from other contributors are reviewed by a reviewer before they are merged. Every signing request is approved by an approver.

## Privacy

Lucid Viewer does not send information to other systems unless you ask it to or the person who installed it asks it to.

The only network access is the download of the background removal model. The model is downloaded from GitHub:

- the first time you choose **Remove background**, or
- during setup, when you select the model

Sharing a photo uses the Windows share window. The photo goes only where you send it.

## Installation and removal

The installer shows each change it makes on its pages. These include shortcuts, the Explorer menu, file associations and starting with Windows. You can choose each one.

To remove the program, use **Settings > Apps**. The uninstaller asks if you want to keep your settings. See [[Installation|Installation]].
