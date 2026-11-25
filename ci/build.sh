#!/bin/bash
# Peanuts Native for Linux: the whole build, from the newest Peanuts Native to the AppImage, on a native arm64 Ubuntu 24.04 machine
# (GitHub's ubuntu-24.04-arm runner; .github/workflows/release.yml runs it). Nothing is cross-compiled here.
#
#   ci/build.sh <version> <out folder>
#     env: UPSTREAM_REF   the igawa6/peanuts-native commit or branch to build (default: its default branch)
#          COMPANIONS_REF the igawa6/eden-duo-companions commit (default: its default branch)
#          W              the work folder (default: ./work)
# Writes <out>/PeanutsNative-aarch64.AppImage, its .sha256, <out>/upstream.txt (the commits built) and <out>/notes.md.
set -euo pipefail
VER=${1:?version}; OUT=$(mkdir -p "${2:?out folder}" && cd "$2" && pwd)
HERE=$(cd "$(dirname "$0")/.." && pwd)
W=$(mkdir -p "${W:-$HERE/work}" && cd "${W:-$HERE/work}" && pwd)
J=$(nproc)
PFX=$W/prefix
DL=$W/dl; mkdir -p "$DL" "$PFX"
say() { echo; echo "=== $*"; }
fetch() { [ -s "$DL/$2" ] || curl -fL --retry 4 -o "$DL/$2" "$1"; [ -z "${3:-}" ] || echo "$3  $DL/$2" | sha256sum -c -; }
[ "$(uname -m)" = aarch64 ] || { echo "ci/build.sh builds natively on arm64 (this is $(uname -m))"; exit 1; }

say "tools"
SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
$SUDO apt-get update -q
$SUDO apt-get install -y -q --no-install-recommends clang lld llvm cmake ninja-build pkg-config patch patchelf squashfs-tools zstd git curl unzip ca-certificates \
  gcc g++ python3 python3-fonttools \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libegl-dev libgles-dev libgl-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libudev-dev \
  libdbus-1-dev libdrm-dev libgbm-dev libvulkan-dev zlib1g-dev
export CC=clang CXX=clang++
CLANG_TC=$HERE/ci/native-clang.cmake

say "Peanuts Native (upstream)"
rm -rf "$W/pn"
git clone -q --filter=blob:none https://github.com/igawa6/peanuts-native.git "$W/pn"
( cd "$W/pn" && git checkout -q "${UPSTREAM_REF:-HEAD}" )
UP=$(cd "$W/pn" && git rev-parse HEAD); UP_DATE=$(cd "$W/pn" && git log -1 --format=%cs); PNV=$(cat "$W/pn/peanuts/VERSION")
echo "Peanuts Native $PNV, $UP ($UP_DATE)"
python3 "$HERE/scripts/patch_upstream.py" "$W/pn"

say "SDL 3.2.26"
fetch https://github.com/libsdl-org/SDL/releases/download/release-3.2.26/SDL3-3.2.26.tar.gz SDL3-3.2.26.tar.gz
rm -rf "$W/sdl" && mkdir -p "$W/sdl" && tar xzf "$DL/SDL3-3.2.26.tar.gz" -C "$W/sdl" --strip-components=1
cmake -S "$W/sdl" -B "$W/sdl/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PFX" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF \
  -DSDL_INSTALL_DOCS=OFF -DSDL_CAMERA=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_IBUS=OFF -DSDL_KMSDRM=OFF -DSDL_WAYLAND_LIBDECOR=OFF >/dev/null
cmake --build "$W/sdl/b" -j"$J" >/dev/null && cmake --install "$W/sdl/b" >/dev/null

say "FFmpeg 7.1.2 (H.264 and AAC decoders, static)"
fetch https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz ffmpeg-7.1.2.tar.xz
rm -rf "$W/ffmpeg" && mkdir -p "$W/ffmpeg" && tar xJf "$DL/ffmpeg-7.1.2.tar.xz" -C "$W/ffmpeg" --strip-components=1
( cd "$W/ffmpeg" && ./configure --prefix="$PFX" --cc=clang --enable-pic --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample --disable-postproc --enable-static --disable-shared \
    --enable-decoder='h264,aac,aac_latm' --enable-parser='h264,aac' --enable-bsf=h264_mp4toannexb >/dev/null && make -j"$J" >/dev/null && make install >/dev/null )
cp /usr/lib/aarch64-linux-gnu/libz.a "$PFX/lib/"

say "the game player (peanuts-run, peanuts-bottom)"
cmake -S "$HERE/runtime" -B "$W/rt" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$CLANG_TC" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPN_SRC="$W/pn" -DPNL_PREFIX="$PFX" >/dev/null
cmake --build "$W/rt" -j"$J"

say "the launcher (peanuts-native)"
[ -d "$W/imgui" ] || git clone -q --depth 1 --branch v1.92.5 https://github.com/ocornut/imgui.git "$W/imgui"
[ -d "$W/stb" ] || git clone -q --depth 1 https://github.com/nothings/stb.git "$W/stb"
cmake -S "$HERE/launcher" -B "$W/la" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$CLANG_TC" -DCMAKE_BUILD_TYPE=Release -DIMGUI_DIR="$W/imgui" -DSTB_DIR="$W/stb" -DSDL_PREFIX="$PFX" >/dev/null
cmake --build "$W/la" -j"$J"

say "the importer (nximport_cli)"
cmake -S "$W/pn/dm/peanuts/import" -B "$W/imp" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$CLANG_TC" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$W/imp" -j"$J" --target nximport_cli

say "Python 3.14 with the packer's packages"
fetch https://github.com/astral-sh/python-build-standalone/releases/download/20261123/cpython-3.14.10+20261123-aarch64-unknown-linux-gnu-install_only_stripped.tar.gz py.tar.gz
rm -rf "$W/py" && mkdir -p "$W/py" && tar xzf "$DL/py.tar.gz" -C "$W/py"
PY=$W/py/python
"$PY/bin/python3" -m pip install -q --disable-pip-version-check --no-warn-script-location capstone==5.0.7 lz4==4.4.5 zbic==1.0.1.post1

say "the Oodle worker and the packer's C modules"
rm -rf "$W/pn/peanuts/.build"
CC=gcc CXX=g++ "$PY/bin/python3" - "$W/pn" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1] + '/peanuts/scripts')
import peanuts_build as b
for f in (b.build_unoodle, b.build_nxaes, b.build_nxyaz0):
    r, why = f()
    if not r: sys.exit(f'{f.__name__}: {why}')
    print(f.__name__, r)
EOF
mkdir -p "$W/prebuilt"
cp "$W/pn/peanuts/.build/ooz/unoodle" "$W/pn/peanuts/.build/pymod/"_nx*.so "$W/prebuilt/"

say ".NET 10 runtime and spirv-tools (conda-forge, linux-aarch64)"
conda_pkg() {   # <file> <sha256> -> $W/conda/<name>
	fetch "https://conda.anaconda.org/conda-forge/linux-aarch64/$1" "$1" "$2"
	local d=$W/conda/${1%.conda}; rm -rf "$d" "$d.x"; mkdir -p "$d" "$d.x"
	( cd "$d.x" && unzip -q "$DL/$1" && for t in pkg-*.tar.zst; do zstd -dcq "$t" | tar xf - -C "$d"; done ); rm -rf "$d.x"
}
conda_pkg dotnet-runtime-10.0.15-h4dd03ba_1.conda "$(sed -n 's/^dotnet-runtime \(.*\)$/\1/p' "$HERE/ci/pins.txt")"
conda_pkg spirv-tools-2026.4-hde6636f_2.conda "$(sed -n 's/^spirv-tools \(.*\)$/\1/p' "$HERE/ci/pins.txt")"

say "fonts and the AppImage runtime"
fetch https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip inter.zip
rm -rf "$W/inter" && mkdir -p "$W/inter" && unzip -q -j "$DL/inter.zip" extras/ttf/Inter-Regular.ttf extras/ttf/Inter-SemiBold.ttf extras/ttf/InterDisplay-Bold.ttf LICENSE.txt -d "$W/inter"
mkdir -p "$W/fonts"
cp "$W/pn/dm/third_party/fonts/NotoSans-Regular-subset.ttf" "$W/fonts/standard.ttf"
python3 "$W/pn/dm/tools/make_ext_font.py" "$W/fonts/nintendo_ext.ttf" "$W/pn/dm/third_party/fonts/NotoSans-Regular-subset.ttf"
fetch https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-aarch64 runtime-aarch64

say "the Eden Duo companions (reader modules for linux-aarch64)"
rm -rf "$W/edc" && git clone -q --filter=blob:none https://github.com/igawa6/eden-duo-companions.git "$W/edc"
( cd "$W/edc" && git checkout -q "${COMPANIONS_REF:-HEAD}" )
EDC=$(cd "$W/edc" && git rev-parse HEAD)
[ -d "$W/eden" ] || git clone -q --depth 1 https://github.com/igawa6/eden-duo.git "$W/eden"
C=$W/eden/.cache/cpm; mkdir -p "$C/fmt" "$C/nlohmann_json" "$C/zstd" "$C/lz4"
[ -d "$C/fmt/12.1.0" ] || git clone -q --depth 1 --branch 12.1.0 https://github.com/fmtlib/fmt.git "$C/fmt/12.1.0"
[ -d "$C/nlohmann_json/v3.12.0" ] || git clone -q --depth 1 --branch v3.12.0 https://github.com/nlohmann/json.git "$C/nlohmann_json/v3.12.0"
[ -d "$C/zstd/latest" ] || git clone -q --depth 1 https://github.com/facebook/zstd.git "$C/zstd/latest"
[ -d "$C/lz4/v1.10.0" ] || git clone -q --depth 1 --branch v1.10.0 https://github.com/lz4/lz4.git "$C/lz4/v1.10.0"
# GCC: clang with libstdc++ refuses the modules' recursive std::pair<std::string, Value> (lp_unity.h)
CC=gcc CXX=g++ cmake -S "$W/edc/native/modules" -B "$W/dsm" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DEDEN_ACNH_BUILD_DIAGNOSTICS=OFF \
  -DEDEN_SOURCE_ROOT="$W/eden" -DCMAKE_C_FLAGS="-ffile-prefix-map=$W/=/src/" -DCMAKE_CXX_FLAGS="-ffile-prefix-map=$W/=/src/" >/dev/null
T=$(ninja -C "$W/dsm" -t targets all | grep -o "^dsmod-[a-z0-9-]*:" | sort -u | tr -d ':' | grep -v -- "-tool$\|-lib$" | tr '\n' ' ')
ninja -C "$W/dsm" -k 0 $T || echo "(some companion modules did not build; their packages are left out)"
rm -rf "$W/companions"
USER=x LOGNAME=x STRIP=strip python3 -I "$HERE/scripts/build_companions.py" "$W/edc" "$W/dsm" "$W/companions" | tee "$W/companions.txt"

say "the AppImage $VER"
B=$W RUNTIME=$DL/runtime-aarch64 SYSROOT=$PFX INTER=$W/inter STRIP=strip PYTHON_DIR=$PY \
  DOTNET_DIR=$W/conda/dotnet-runtime-10.0.15-h4dd03ba_1/lib/dotnet SPIRV_DIR=$W/conda/spirv-tools-2026.4-hde6636f_2 \
  UPSTREAM_COMMIT=$UP sh "$HERE/scripts/build_appimage.sh" "$VER" "$OUT/PeanutsNative-aarch64.AppImage"
# the bundle check: every program starts and the TLS slot is where the packs expect it
# (each prints its usage and exits non-zero: the output is kept and looked at, as pipefail would count the exit status)
o=$("$W/AppDir/usr/bin/peanuts-run" 2>&1 || true); grep -q "usage: peanuts-run" <<<"$o" || { echo "peanuts-run does not start:"; echo "$o"; exit 1; }
o=$("$W/AppDir/usr/bin/peanuts-bottom" 2>&1 || true); grep -q "usage:" <<<"$o" || { echo "peanuts-bottom does not start:"; echo "$o"; exit 1; }
( cd "$OUT" && sha256sum PeanutsNative-aarch64.AppImage > PeanutsNative-aarch64.AppImage.sha256 )
printf 'peanuts-native\t%s\t%s\t%s\neden-duo-companions\t%s\nport\t%s\n' "$PNV" "$UP" "$UP_DATE" "$EDC" "$(cd "$HERE" && git rev-parse HEAD 2>/dev/null || echo unknown)" > "$OUT/upstream.txt"
{
  echo "Peanuts Native for Linux $VER (aarch64, for the AYN Thor on ArmadaOS)"
  echo
  echo "- Peanuts Native $PNV, upstream commit ${UP:0:12} ($UP_DATE)"
  echo "- Port $(cd "$HERE" && git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
  echo "- Eden Duo companions ${EDC:0:12}: $(grep -c '^  [0-9A-F]\{16\}' "$W/companions.txt" || true) packages"
  echo "- Build identity $(cat "$W/AppDir/usr/share/peanuts-native/engine/PNL_ENGINE_VERSION"): games built with another one are offered a rebuild"
  echo
  echo "Update from inside the app (Settings → Updates), or download PeanutsNative-aarch64.AppImage below."
} > "$OUT/notes.md"
cat "$OUT/notes.md"
