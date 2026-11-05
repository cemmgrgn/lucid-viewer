# Building Lucid Viewer

## Requirements

- Windows 10 or 11, 64-bit
- Visual Studio 2026 (or 2022) with the **Desktop development with C++** workload, including
  - MSVC v145 (v143 for Visual Studio 2022)
  - C++ ATL for the latest build tools
  - Windows 11 SDK (10.0.26100 or later)
- [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) for the installer (optional)

## Get the source

```
git clone https://github.com/cemmgrgn/lucid-viewer.git
cd lucid-viewer
git submodule update --init deps/WTL-sf
```

Only the WTL submodule is needed. The image libraries (libjpeg-turbo, libpng, libwebp, libjxl, libheif, libavif,
LibRaw, Little CMS) are committed as prebuilt binaries in `src/LucidViewer/lib*`. Their sources are submodules in
`extras/third_party` with build scripts in `extras/scripts`, see [extras/third_party/README.md](../extras/third_party/README.md).

## Build

Open `src/LucidViewer.sln` in Visual Studio and build **Release | x64**, or from a Developer Command Prompt:

```
msbuild src\LucidViewer.sln /t:WICLoader /t:LucidViewer /p:Configuration=Release /p:Platform=x64
```

With Visual Studio 2022 add `/p:PlatformToolset=v143`.

The program and everything it needs is in `src\LucidViewer\bin\x64\Release`. That folder runs as it is.

### ARM64 (Windows on ARM)

The prebuilt libraries are x86/x64 only. For **Release | ARM64** they come from [vcpkg](https://vcpkg.io), built for
`arm64-windows` (the CI workflow does the same, see `.github/workflows/build.yml`):

```
vcpkg install libjpeg-turbo libpng[apng] zlib libwebp libjxl libheif libavif[dav1d] lcms libraw --triplet arm64-windows
```

Copy `installed\arm64-windows\include`, `lib` and `bin` of vcpkg to `deps\arm64`, then (with the ARM64 build tools and
"C++ ATL for latest build tools (ARM64/ARM64EC)" installed in Visual Studio):

```
msbuild src\LucidViewer.sln /t:WICLoader /t:LucidViewer /p:Configuration=Release /p:Platform=ARM64
```

The SSE/AVX code of the image pipeline is x86 only, ARM64 uses the C++ implementation. The installer is built with
`iscc /DArch=arm64 installer\LucidViewer.iss`.

## Installer

```
iscc installer\LucidViewer.iss
```

The installer is written to `installer\Output`. Options for testing, e.g. `/DNoLicensePage`, are described at the top of the script.

## Repository layout

| Folder | Contents |
|---|---|
| `src/LucidViewer` | The program: C++ sources, resources, `Config` (settings, string tables, key map) and prebuilt image libraries |
| `src/WICLoader` | Small DLL that decodes images with the Windows Imaging Component |
| `installer` | Inno Setup script and wizard images |
| `tests/UnitTests` | Unit tests of `src/LucidViewer/TextUtils.h`, built and run by the CI workflow |
| `tools/bench` | Startup benchmark used for the numbers in the README |
| `docs` | README images, logo, building notes, history of JPEGView |
| `site` | The website (GitHub Pages), published by `.github/workflows/pages.yml` with the images from `docs` |
| `deps` | WTL (submodule) |
| `extras` | Sources of the image libraries (submodules) and the scripts to build them and the string tables |

## Measuring startup

```
python tools\bench\startup_bench.py --make-image bench24.jpg
python tools\bench\startup_bench.py --name "Lucid Viewer" --image bench24.jpg --runs 10 -- src\LucidViewer\bin\x64\Release\LucidViewer.exe "{image}"
```

Set the environment variable `LUCID_TRACE=1` to get the time of each startup step in `%TEMP%\lucid_trace.txt`.
