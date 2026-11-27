# Peanuts Native for Linux (aarch64)

A Linux port of [Peanuts Native](https://github.com/igawa6/peanuts-native) for arm64 handhelds — made for the **AYN Thor on ArmadaOS** —
packaged as one AppImage. It turns Switch games you own into native apps: the game's ARM64 code runs directly on the CPU, its graphics are
replayed on Vulkan (Mesa Turnip), and its shaders are translated ahead of time. No emulator.

Bring your own `prod.keys`, firmware (optional) and game dumps. Nothing from Nintendo is included or downloaded.

## Using it

1. Copy `PeanutsNative-1.2.5-aarch64.AppImage` to the Thor (e.g. `/var/home/armada/AppImages/`), make it executable
   (`chmod +x`, or Properties → Permissions in Dolphin) and start it. To start it from Game Mode, add it to Steam as a non-Steam game
   (or add the games themselves: see *Steam shortcuts* below).
2. The first start copies the build tools into `~/.local/share/peanuts-native/engine` (once per release, about 650 MB).
3. Setup: pick your `prod.keys` (keys from Eden, Ryujinx, yuzu and EmuDeck folders are found automatically), optionally your firmware
   (folder or `.zip`, for the console's fonts and Miis), and the folder with your `.nsp` / `.xci` / `.nsz` / `.xcz` / `.nup` files.
4. Pick a game → **Build** (5–40 minutes on the Thor) → **Play**.
5. In a game: press **Select + Start** together for the in-game menu (Resume, Bottom screen shown / hidden, Frame rate cap, Frame rate counter, Quit game; D-pad or left stick to move, A to choose, B to close; F1 on a keyboard). The game keeps running behind it and gets no button presses while it is open.
6. Hold **Select + Start** for 1.5 seconds to quit (Settings → *Quit a game* can make it L + R + Select + Start or L3 + R3; Shift+Esc on a keyboard). F11 toggles full screen.

**Max clocks:** Settings → *Set up max clocks* installs a tiny root helper once (`/etc/peanuts-native/pn-clocks` and a sudoers rule
for your user only), so every game runs with the CPU on the performance governor at its top frequency and the GPU at its top frequency,
and both go back to normal when the game ends. On by default; per game in Game settings. If no password prompt appears (Game Mode has none),
run `sudo sh ~/.local/share/peanuts-native/install-max-clocks.sh` once in Konsole in Desktop Mode.

**Bottom screen (Eden Duo companions):** games with an Eden Duo companion show it on the Thor's bottom screen, with touch, while the game
plays on the top screen — the same pages as Eden Duo. 24 companions come with the AppImage (Persona 5 Royal, Mario Odyssey, Mario Wonder,
Mario Kart 8 Deluxe, Metroid Dread, Pokémon Legends Z-A, Sword/Shield, Brilliant Diamond/Shining Pearl, Animal Crossing, Fire Emblem Three Houses,
Echoes of Wisdom, Persona 3 Portable, Link's Awakening, Cult of the Lamb and more); the game's page says when one is there. In Game Mode the window goes through ArmadaOS's
`armada-run-bottom` onto the bottom screen and claims it above ArmadaLive (as the Thor builds of Eden Duo do); in Desktop Mode it opens
full screen on the other monitor. Turn it off per game in Game settings, or import another `.dsmod.zip` there (it needs a
`linux-aarch64` module if the companion reads game memory). Its log: `…/logs/<title ID>-bottom.log`.

**Steam shortcuts:** a built game's page has **Add to Steam**: Steam gets a shortcut that starts that game directly
(`PeanutsNative….AppImage --launch <title ID>`, with max clocks and the bottom screen), with library, wide and hero artwork made from the
game's icon. Settings → *Add all built games to Steam* does every built game at once. Steam must be closed while shortcuts are added (it rewrites
its list when it exits): do it from Desktop Mode — Peanuts Native offers to close Steam for you — and the games show in Game Mode under
Library → Non-Steam. From a terminal: `PeanutsNative….AppImage --add-to-steam <title ID>`. If you move the AppImage, add the games again.

**Mods:** Game settings → *Mods* → *Add a mod* takes a mod folder or `.zip` in the usual Atmosphère / Eden layout (`romfs`, `exefs`,
`exefs_patches`, also inside `atmosphere/contents/<title ID>/`). Game-file mods (`romfs`) are layered over the game at every start, first one
wins, each with its own on/off switch. Code mods (`exefs` `.ips` or `.pchtxt` patches, replaced modules, `exefs_patches`) are applied when the game is built,
so adding, removing or switching one asks for a build. Cheats inside a mod are kept but not used yet.

**Updates & DLC folder:** Settings → *Updates & DLC folder* points at a separate folder of updates and DLC (any depth); they are matched to the
games in your games folder by title ID, and games found only there are not listed.

**Game version:** with more than one update of a game, Game settings → *Update to build with* picks the one the next
build uses (Automatic: the update the game was last built from, else the newest one Peanuts Native's profile supports). An untested update may not start.

**Graphics:** games start with `DM_VK_GPL=0` (whole pipelines instead of linked pipeline libraries): with Mesa 26.3's Turnip, linked pipelines drew
some games' scenery wrong. `DM_VK_GPL=1` in a game's extra environment turns them back on. Mystery Dungeon DX also starts with `DM_VK_POOL_FB=0` (its menu art and effects were scrambled
with the texture pool reads on), `DM_VK_CS_ASYNC=0` and `DM_VK_GUEST_UNCACHED=1` (its models burst into spikes and its labels garbled now and then: the GPU read the game's memory through the CPU cache); if another game shows scrambled menus or effects, try that line in its extra environment.

**Updates:** when Peanuts Native (igawa6/peanuts-native), the Eden Duo companions or this port change, GitHub Actions builds a new
AppImage on a native arm64 machine (`ci/build.sh`, `.github/workflows/release.yml`, checked every 6 hours) and publishes it as a release. The app
checks for it when it starts (Settings → *Updates*) and asks before it updates: it downloads the new AppImage, checks its SHA-256 and puts it in place of
the running one (the previous one stays as `<name>.old`), so Steam shortcuts keep working. If an upstream change does not fit the port's edits,
the build fails and nothing is published. A new Peanuts Native version (its `peanuts/VERSION`) gives the build engine a new identity, so games are
offered **Build again** then; other updates keep them ready. From a terminal: `PeanutsNative….AppImage --check-update`.

Where things are: builds `~/.local/share/peanuts-native/builds/<title ID>`, saves `…/saves/<title ID>`, logs `…/logs`,
settings `~/.config/peanuts-native/settings.ini`.

## What is in the AppImage

| | |
|---|---|
| `peanuts-native` | the launcher (SDL3 + Dear ImGui): setup, library, builds, game settings, controller-first |
| `peanuts-run` | the game player: Peanuts Native's host (the Android build's `libnxhost`, generic host) built for glibc, with an SDL3 front end |
| `nximport_cli` | Peanuts Native's importer (`.nsp/.xci/.nsz/.xcz` → `.nup`, icons, firmware fonts) |
| Python 3.14 + capstone, lz4, zbic | the packer (`peanuts/scripts/peanuts_build.py` → `scripts/pack_android.py`) |
| .NET 10 runtime | runs the repository's locked shader translator (`dm/gfx/tools/dmsh_xlate_bin`) |
| spirv-val | the shader build's validator |

Packs are made by upstream's own PC builder, unchanged, so a build here is the same pack the Android app makes.

## How the port works

* **Same packs as Android.** The packer rewrites game code to reach its per-thread slots through Bionic's `TLS_SLOT_APP`
  (`[tpidr_el0 + 0x10]`). On glibc arm64 that word is the first word of the main executable's static TLS block, so `peanuts-run`
  makes its first thread-local variable that word (`runtime/src/pnl_tls.c`, checked at link time by `runtime/check_tls.py` and at start
  by `pnl_tls_check`). Android packs then run unchanged.
* **Android NDK APIs on Linux** (`runtime/shim`): `ANativeWindow` → the SDL3 window (the Vulkan surface comes from SDL: Wayland or X11),
  AAudio → SDL3 audio, AMediaCodec (H.264, AAC) → FFmpeg's decoders, the Android log → stderr, system properties → device tree / DMI.
  The host compiles with `-D__ANDROID__` against these headers; four small upstream edits (`scripts/patch_upstream.py`, all behind `PEANUTS_LINUX`)
  make the replay take SDL's surface and the `libvulkan.so.1` loader.
* **Input**: SDL3 gamepads are fed to the host as Android key/axis events (`runtime/src/pnl_run.c`), gyro from the controller's sensors,
  touch from the window, rumble back to the pad.

* **Bottom screen**: the host renders the companion page (`nxhost_peanuts_frame`, as the Android app's second-display page polls it);
  `runtime/src/pnl_companion.c` copies each frame into a shared file and `peanuts-bottom` (`runtime/src/peanuts_bottom.c`) shows it on the
  bottom screen and writes touches back. It is a separate program because in Game Mode the bottom screen is its own Xwayland display.
  The companions' reader modules are the eden-duo-companions sources built for `linux-aarch64` (`scripts/build_companions.py`).

Not in this version: dump *folders* in the library
(the builder takes them, the launcher lists only files), games the PC builder refuses (those that need a boot on the device to capture shaders).

## Building

On an x86-64 Ubuntu 24.04 machine with `clang lld gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libc6-dev-arm64-cross cmake ninja-build squashfs-tools`:

1. SDL 3.2 for aarch64 (dynamic X11/Wayland/PipeWire/PulseAudio/ALSA), FFmpeg 7.1 (`--enable-decoder=h264,aac,aac_latm`, static) and zlib into one prefix.
2. `scripts/patch_upstream.py <peanuts-native tree>`.
3. `cmake -S runtime -B rt -DPN_SRC=<tree> -DPNL_PREFIX=<prefix> -DCMAKE_TOOLCHAIN_FILE=<aarch64 clang toolchain>` and build: `peanuts-run`.
4. `cmake -S launcher -B la -DIMGUI_DIR=… -DSTB_DIR=… -DSDL_PREFIX=<prefix>` (same toolchain): `peanuts-native`.
5. `dm/peanuts/import` for aarch64 (`nximport_cli`); `dm/third_party/ooz/build.sh` with the aarch64 compiler (`unoodle`);
   `_nxaes` / `_nxyaz0` against python-build-standalone 3.14 aarch64's headers.
6. python-build-standalone 3.14 aarch64 with the capstone, lz4 and zbic aarch64 wheels in site-packages; conda-forge linux-aarch64
   `dotnet-runtime` 10 and `spirv-tools`; AppImage `type2-runtime` for aarch64.
7. Companions: the eden-duo-companions `native/modules` built for aarch64 (GCC; `-DEDEN_SOURCE_ROOT=<eden-duo tree>`), then
   `scripts/build_companions.py <eden-duo-companions> <modules build dir> $B/companions`.
8. `scripts/build_appimage.sh <version> <out.AppImage>` (the build identity comes from `ci/engine-compat.tsv`, else the Peanuts Native version).

Or all of it at once, natively on an arm64 Ubuntu 24.04 machine: `ci/build.sh <version> <out folder>` (what the release workflow runs).

## License

GPL-3.0-or-later, as Peanuts Native. See `THIRD-PARTY.md` for the bundled parts and their licenses.
