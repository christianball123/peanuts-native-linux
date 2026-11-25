#!/bin/sh
# Assembles Peanuts Native for Linux (aarch64) into an AppImage, from parts built beforehand (see README.md, "Building"):
#   $B/rt/peanuts-run        the game player (runtime/, cross-built for aarch64)        $B/la/peanuts-native   the launcher (launcher/)
#   $B/imp/nximport_cli      the importer (dm/peanuts/import)                            $B/prebuilt/           unoodle, _nxaes, _nxyaz0 for aarch64 / Python 3.14
#   $B/py-a64/python         Python 3.14 aarch64 (python-build-standalone) with capstone, lz4, zbic in site-packages
#   $B/pn                    the patched peanuts-native tree                             $CONDA/                dotnet-runtime and spirv-tools (conda-forge, linux-aarch64)
#   $B/companions/<title id>  the Eden Duo companion packages (scripts/build_companions.py)  $B/rt/peanuts-bottom  the bottom-screen window
#   $B/fonts                 standard.ttf, nintendo_ext.ttf (dm/tools/make_ext_font.py)  $INTER                 Inter-*.ttf
# Usage: build_appimage.sh <version> <out.AppImage>
set -eu
VER=${1:?version}; OUT=${2:?output}
HERE=$(cd "$(dirname "$0")/.." && pwd)
B=${B:-/home/claude/build}; CONDA=${CONDA:-/home/claude/dl/conda}; INTER=${INTER:-/home/claude/deps/inter}; SYSROOT=${SYSROOT:-/opt/a64}
A=$B/AppDir
STRIP=${STRIP:-aarch64-linux-gnu-strip}
rm -rf "$A"; mkdir -p "$A/usr/bin" "$A/usr/lib" "$A/usr/share/peanuts-native/fonts" "$A/usr/share/applications" "$A/usr/share/icons/hicolor/256x256/apps" "$A/usr/share/metainfo"

# programs
cp "$B/la/peanuts-native" "$B/rt/peanuts-run" "$B/rt/peanuts-bottom" "$B/imp/nximport_cli" "$A/usr/bin/"
$STRIP "$A/usr/bin/peanuts-native" "$A/usr/bin/peanuts-run" "$A/usr/bin/peanuts-bottom" "$A/usr/bin/nximport_cli"
for p in peanuts-native peanuts-run peanuts-bottom; do patchelf --set-rpath '$ORIGIN/../lib' "$A/usr/bin/$p"; done   # no build-machine paths
cp "$SYSROOT/lib/libSDL3.so.0" "$A/usr/lib/"; $STRIP "$A/usr/lib/libSDL3.so.0"
# spirv-val and its library (conda-forge spirv-tools; RPATH $ORIGIN/../lib)
SP=${SPIRV_DIR:-$CONDA/spirv-tools-2026.4-hde6636f_2}
cp "$SP/bin/spirv-val" "$A/usr/bin/"; cp "$SP/lib/libSPIRV-Tools.so" "$A/usr/lib/"
# .NET 10 runtime (conda-forge dotnet-runtime) for the shader translator
cp -a "${DOTNET_DIR:-$CONDA/dotnet-runtime-10.0.15-h4dd03ba_1/lib/dotnet}" "$A/usr/lib/dotnet"
# Python 3.14 with the packer's packages
cp -a "${PYTHON_DIR:-$B/py-a64/python}" "$A/usr/python"
rm -rf "$A/usr/python/lib/python3.14/test" "$A/usr/python/lib/python3.14/idlelib" "$A/usr/python/lib/python3.14/tkinter" "$A/usr/python/lib/python3.14/turtledemo" \
       "$A/usr/python/lib/python3.14/ensurepip" "$A/usr/python/share"
find "$A/usr/python" -name __pycache__ -type d -prune -exec rm -rf {} +

# the build engine: the peanuts-native tree without its development parts, with the prebuilt helpers the builder would otherwise compile
E=$A/usr/share/peanuts-native/engine
mkdir -p "$E"
( cd "$B/pn" && tar cf - --exclude=./.git --exclude=./docs --exclude=./wiki --exclude=./android/app --exclude='*/__pycache__' --exclude=./peanuts/.build . ) | ( cd "$E" && tar xf - )
mkdir -p "$E/peanuts/.build/nximport" "$E/peanuts/.build/ooz" "$E/peanuts/.build/pymod"
cp "$A/usr/bin/nximport_cli" "$E/peanuts/.build/nximport/nximport_cli"
cp "$B/prebuilt/unoodle" "$E/peanuts/.build/ooz/unoodle"
cp "$B/prebuilt/"_nx*.so "$E/peanuts/.build/pymod/"
cp "$HERE/builder/pnl_tool.py" "$E/pnl_tool.py"
# The build engine's identity: builds made by another identity are offered a rebuild. A release whose build tools did not change keeps the
# previous identity (ENGINE_VERSION=<that release's PNL_ENGINE_VERSION>), so games built with it stay ready to play.
# Without ENGINE_VERSION: ci/engine-compat.tsv's identity for this Peanuts Native version (a release of upstream), else "pn-<version>"
PNV=$(cat "$B/pn/peanuts/VERSION" 2>/dev/null | tr -d '\n')
[ -n "${ENGINE_VERSION:-}" ] || ENGINE_VERSION=$(awk -F'\t' -v v="$PNV" '$1 == v { print $2 }' "$HERE/ci/engine-compat.tsv" 2>/dev/null)
[ -n "${ENGINE_VERSION:-}" ] || ENGINE_VERSION="pn-$PNV"
echo "$ENGINE_VERSION" > "$E/PNL_ENGINE_VERSION"
# files brought into an installed engine copy without a new identity (core.cpp engine_ok): launcher helpers and patches for builds the identity did not cover
printf '%s\n' dm/games/01003D200BAA2000/patches.py > "$E/PNL_ENGINE_SYNC"
echo "${UPSTREAM_COMMIT:-unknown}" | cut -c1-12 > "$E/PNL_UPSTREAM_COMMIT"
# the copy's content, for an installed copy to know it is older (core.cpp engine_ok)
( cd "$E" && find . -type f ! -name PNL_ENGINE_STAMP -print0 | LC_ALL=C sort -z | xargs -0 sha256sum ) | sha256sum | cut -c1-16 > "$E/PNL_ENGINE_STAMP"

# the launcher's files
S=$A/usr/share/peanuts-native
echo "$VER" > "$S/VERSION"
cp "$INTER/Inter-Regular.ttf" "$INTER/Inter-SemiBold.ttf" "$INTER/InterDisplay-Bold.ttf" "$S/fonts/"
cp "$INTER/LICENSE.txt" "$S/fonts/Inter-LICENSE.txt"
cp "$B/fonts/standard.ttf" "$B/fonts/nintendo_ext.ttf" "$S/fonts/"
python3 "$HERE/builder/pnl_tool.py" catalog "$B/pn/peanuts/games.json" "$S/catalog.tsv"
cp "$HERE/appimage/install-max-clocks.sh" "$S/"
# the bottom screen's companion packages, one folder per title id
[ -d "$B/companions" ] && cp -a "$B/companions" "$S/companions"
cp "$HERE/appimage/peanuts-native.png" "$S/peanuts-native.png"
cp "$HERE/appimage/peanuts-native.png" "$A/usr/share/icons/hicolor/256x256/apps/peanuts-native.png"
cp "$HERE/appimage/peanuts-native.png" "$A/peanuts-native.png"; ln -sf peanuts-native.png "$A/.DirIcon"
cp "$HERE/LICENSE" "$S/LICENSE" 2>/dev/null || cp "$B/pn/LICENSE" "$S/LICENSE"
cp "$HERE/THIRD-PARTY.md" "$S/" 2>/dev/null || true
cp "$HERE/appimage/peanuts-native.desktop" "$A/peanuts-native.desktop"
cp "$HERE/appimage/peanuts-native.desktop" "$A/usr/share/applications/"
cp "$HERE/appimage/io.github.peanuts_native.linux.appdata.xml" "$A/usr/share/metainfo/" 2>/dev/null || true
cp "$HERE/appimage/AppRun" "$A/AppRun"; chmod 755 "$A/AppRun"

# the AppImage: the type 2 runtime for aarch64 (AppImage/type2-runtime) followed by the AppDir as a zstd squashfs, which is all an AppImage is
RUNTIME=${RUNTIME:-$B/runtime-aarch64}
SQ=$B/AppDir.squashfs; rm -f "$SQ"
mksquashfs "$A" "$SQ" -root-owned -noappend -comp zstd -Xcompression-level 15 -b 1M -quiet
cat "$RUNTIME" "$SQ" > "$OUT"; chmod 755 "$OUT"; rm -f "$SQ"
ls -la "$OUT"; sha256sum "$OUT"
