# Building from source

This page is a short summary. The full instructions are in [docs/BUILDING.md](https://github.com/cemmgrgn/lucid-viewer/blob/main/docs/BUILDING.md).

## Requirements

- Windows 10 or 11, 64-bit
- Visual Studio 2026, or Visual Studio 2022, with the **Desktop development with C++** workload
- MSVC v145 (v143 with Visual Studio 2022)
- C++ ATL for the latest build tools
- Windows 11 SDK, version 10.0.26100 or later
- [Inno Setup 6.3](https://jrsoftware.org/isinfo.php) or later, only if you want to build the installer

## Get the source

```
git clone https://github.com/cemmgrgn/lucid-viewer.git
cd lucid-viewer
git submodule update --init deps/WTL-sf
```

Only the WTL submodule is needed for a normal build. The image libraries are already in the repository as prebuilt binaries.

## Build

Open `src/LucidViewer.sln` in Visual Studio and build **Release | x64**. Or run this in a Developer Command Prompt:

```
msbuild src\LucidViewer.sln /t:WICLoader /t:LucidViewer /p:Configuration=Release /p:Platform=x64
```

With Visual Studio 2022, add `/p:PlatformToolset=v143`.

The program is written to `src\LucidViewer\bin\x64\Release`. That folder runs as it is.

## Build the installer

```
iscc installer\LucidViewer.iss
```

The installer is written to `installer\Output`.

## Repository layout

| Folder | Contents |
|---|---|
| `src/LucidViewer` | The program: sources, resources, settings and the image libraries |
| `src/WICLoader` | A small DLL that decodes images with the Windows Imaging Component |
| `installer` | The Inno Setup script and wizard images |
| `tools/bench` | The startup benchmark |
| `docs` | Images, the logo and the building notes |
| `deps` | WTL, as a submodule |
| `extras` | Sources of the image libraries and the build scripts |

## Measure the startup time

Set the environment variable `LUCID_TRACE=1` before you start the program. It writes the time of each startup step to `%TEMP%\lucid_trace.txt`.

The benchmark script `tools/bench/startup_bench.py` measures how long a program takes to show an image:

```
python tools\bench\startup_bench.py --make-image bench24.jpg
```

## Contributing

Read [CONTRIBUTING.md](https://github.com/cemmgrgn/lucid-viewer/blob/main/CONTRIBUTING.md) before you open a pull request. Keep changes small, and measure the startup time when a change affects it.
