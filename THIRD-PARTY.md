# Peanuts Native for Linux — what is inside

Peanuts Native for Linux is licensed **GPL-3.0-or-later** (LICENSE), as Peanuts Native is. It bundles:

| Part | Where from | License |
|---|---|---|
| Peanuts Native (host, packer, game profiles, companion runtime) | github.com/igawa6/peanuts-native | GPL-3.0-or-later as a whole; parts MIT (nxnative by seedds, Banana Native by moceure) — see engine/LICENSE, engine/android/NOTICE.md, engine/dm/third_party/README.md |
| Eden Duo companion packages and their reader modules (usr/share/peanuts-native/companions) | github.com/igawa6/eden-duo-companions | GPL-3.0 |
| SDL 3 | libsdl.org | zlib |
| Dear ImGui | github.com/ocornut/imgui | MIT |
| stb_image, stb_image_write, stb_truetype | github.com/nothings/stb | public domain / MIT |
| FFmpeg (libavcodec, libavutil: H.264 and AAC decoders only) | ffmpeg.org | LGPL-2.1-or-later |
| zlib | zlib.net | zlib |
| Python 3.14 (python-build-standalone) | github.com/astral-sh/python-build-standalone | PSF and the licenses in usr/python |
| Capstone 5, python-lz4, zbic | PyPI | BSD-3-Clause, BSD-3-Clause, GPL-2.0 |
| .NET 10 runtime | conda-forge dotnet-runtime | MIT |
| SPIRV-Tools | conda-forge spirv-tools | Apache-2.0 |
| Inter (launcher font) | rsms.me/inter | SIL OFL 1.1 |
| Noto Sans subset (stand-in game font) | dm/third_party/fonts | SIL OFL 1.1 |

It contains no Nintendo code, keys or firmware, never downloads any, and plays only games you dumped from your own console.
