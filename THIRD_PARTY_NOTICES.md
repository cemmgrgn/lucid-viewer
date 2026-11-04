# Third-party software

Lucid Viewer is licensed under the GNU General Public License v2 or later (see [LICENSE.txt](LICENSE.txt)).
It is based on [JPEGView](https://github.com/sylikc/jpegview) by David Kleiner and Kevin M (sylikc), GPL v2.

It uses the following libraries and data, each under its own license:

| Component | Used for | License |
|---|---|---|
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | JPEG | IJG, BSD-3-Clause, zlib |
| [libpng](https://github.com/glennrp/libpng) with the APNG patch | PNG, APNG | PNG Reference Library License v2 |
| [zlib](https://github.com/madler/zlib) | PNG | zlib |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) | WebP | BSD-3-Clause |
| [libjxl](https://github.com/libjxl/libjxl) | JPEG XL | BSD-3-Clause |
| [Brotli](https://github.com/google/brotli) | JPEG XL | MIT |
| [libheif](https://github.com/strukturag/libheif) | HEIF, HEIC, AVIF | LGPL-3.0 |
| [libde265](https://github.com/strukturag/libde265) | HEIC (HEVC) | LGPL-3.0 |
| [dav1d](https://code.videolan.org/videolan/dav1d) | AVIF (AV1) | BSD-2-Clause |
| [libavif](https://github.com/AOMediaCodec/libavif) | AVIF | BSD-2-Clause |
| [LibRaw](https://github.com/LibRaw/LibRaw) | Camera RAW | LGPL-2.1 or CDDL-1.0 |
| dcraw by Dave Coffin (modified) | Camera RAW thumbnails | dcraw license (free use and modification) |
| [Little CMS](https://github.com/mm2/Little-CMS) | Color management | MIT |
| [QOI](https://github.com/phoboslab/qoi) | QOI | MIT |
| [WTL](https://sourceforge.net/projects/wtl/) | User interface framework (headers) | MS-PL |
| [U²-Net](https://github.com/xuebinqin/U-2-Net) model (`u2net.onnx`, downloaded on request) | Background removal | Apache-2.0 |

The LGPL libraries are dynamically linked (`heif.dll`, `libde265.dll`, `libraw.dll`) and can be replaced by the user.
Their sources are referenced as submodules in [extras/third_party](extras/third_party).

Windows components used through system APIs (Windows Imaging Component, Windows.Media.Ocr, Windows ML) are part of Windows and not redistributed.
