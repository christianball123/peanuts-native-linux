#!/usr/bin/env python3
"""Peanuts Native for Linux: the launcher's helper, run with the bundled Python inside the build engine (a copy of the peanuts-native tools).

  pnl_tool.py launch <pack folder> <game .nup> <data folder>
      Everything the host needs to start a built game, as KEY=value lines on stdout (what the Android launcher's GameActivity.start and
      GenericProfile.envLines work out): DM_ROMFS, the add-on links (DM_AOC_DIR), the generic host's runtime profile (DM_PROFILE_*), the pack's
      title / version / application. Errors: a line "error: <text>" and exit code 1.
  pnl_tool.py probe-nup <file.nup>
      The title of a .nup the launcher found in the games folder: lines "title <id>", "app <id>", "version <n>", "name <text>", "addons <n>".
  pnl_tool.py catalog <games.json> <out.tsv>
      The supported games (peanuts/games.json) as tab-separated rows for the launcher: title ID, name, builds (display=number,...), companion.
  pnl_tool.py import-mod <title id> <mod folder or .zip> <mods folder> [name]
      A mod (Atmosphère / Eden / yuzu layout: romfs, exefs, exefs_patches, cheats, at any depth) copied into <mods folder>/<name>/ with those folders
      at its top. Prints "name=<name>" and "kinds=<romfs,exefs,...>".
  pnl_tool.py extract <file.nup> <path inside> <out file>
      One file out of a .nup, e.g. base/exefs/main or base/romfs/Data/Managed/Metadata/global-metadata.dat (update/<version>/... for an update).
  pnl_tool.py pchtxt <file.pchtxt> <out folder>
      An IPSwitch text patch (yuzu / Eden: exefs/*.pchtxt) as the IPS32 file Atmosphère and the builder read: <out folder>/<its @nsobid>.ips, from its
      @enabled blocks (offsets plus @flag offset_shift; values as the bytes written, or "text"). Prints "ips=<path> records=<n>".
  pnl_tool.py import-dsmod <file.dsmod.zip> <companions folder>
      An Eden Duo companion package, checked and unpacked into <companions folder>/<title id>/. Prints "title_id=<id>" and "name=<text>".
"""
import json, os, struct, sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parent
sys.path.insert(0, str(ENGINE / 'dm' / 'tools'))
sys.path.insert(0, str(ENGINE / 'dm' / 'peanuts' / 'profile'))


def fail(msg):
    print(f'error: {msg}')
    sys.exit(1)


def read_pack(d):
    rows = {}
    p = d / 'pack.tsv'
    if not p.is_file(): fail(f'{d} has no pack.tsv: build the game again')
    for line in p.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'): continue
        k, _, v = line.partition('\t')
        rows.setdefault(k, []).append(v)
    one = lambda k: (rows.get(k) or [''])[0]
    if one('format') != '1': fail(f'pack format {one("format")!r}, this launcher reads 1: build the game again')
    if one('target') != 'android': fail(f'not a pack for the Android host (target {one("target")!r}): build the game again')
    return rows, one


def selector_of(romfs_rel, version):
    """pack.tsv's romfs row names <name>.nup:<selector>/romfs; a pack made from a folder names the version's package (Pack.romfsSelector)"""
    for ext in ('.nup:', '.nsp:'):
        k = romfs_rel.rfind(ext)
        if k >= 0:
            rest = romfs_rel[k + len(ext):]
            for sel in ('base',):
                if rest == sel or rest.startswith(sel + '/'): return sel
            if rest.startswith('update/'):
                v = rest[len('update/'):].split('/', 1)[0]
                if v.isdigit(): return f'update/{int(v)}'
            if rest.startswith('addon/'): return rest.split('/', 2)[0] + '/' + rest.split('/', 2)[1].lower()
    try: v = int(version)
    except ValueError: v = 0
    return 'base' if v == 0 else f'update/{v}'


def launch(pack_dir, nup_path, data):
    import nup
    pack_dir, nup_path, data = Path(pack_dir).resolve(), Path(nup_path).resolve(), Path(data).resolve()
    rows, one = read_pack(pack_dir)
    tid = one('title_id').upper()
    for m in one('modules').split():
        if not (pack_dir / f'{m}.patched.bin').is_file(): fail(f'the build has no {m}.patched.bin: build the game again')
    if not nup_path.is_file(): fail(f'the game file {nup_path} is gone: build the game again')
    n = nup.Nup(str(nup_path))
    sel = selector_of(one('romfs'), one('version'))
    pkg = next((p for p in n.packages if p.selector == sel), None)
    if pkg is not None and pkg.romfs == nup.NONE and sel.startswith('update/'):   # an update without a RomFS of its own: the base's
        pkg = next((p for p in n.packages if p.kind == nup.BASE), None); sel = 'base'
    if pkg is None or pkg.romfs == nup.NONE: fail(f'{nup_path.name} has no RomFS in {sel}: build the game again')
    out = [f'DM_ROMFS={nup_path}:{sel}/romfs', f'PNL_TITLE_ID={tid}', f'PNL_NAME={one("name")}', f'PNL_VERSION={one("version")}']
    app = one('application') or tid
    out.append(f'PNL_SAVE_ID={app.upper()}')
    # the main module's whole build ID (dev.peanuts.build.GameEnv.ensureBuildId): a companion's reader module may list whole IDs and refuse the 8-byte prefix the host has
    main_id = next((v.partition('=')[2] for v in rows.get('build', []) if v.partition('=')[0] == 'main'), '')
    if len(main_id) >= 16 and all(c in '0123456789abcdefABCDEF' for c in main_id): out.append(f'PN_BUILD_ID={main_id.upper()}')
    # add-ons: DM_AOC_DIR holds <index>.nup → the .nup that has that add-on (Pack.aocLinks); the old links go first
    aoc = data / 'aoc' / tid
    aoc.mkdir(parents=True, exist_ok=True)
    for f in aoc.iterdir():
        if f.is_symlink() or f.is_file(): f.unlink()
    have = {p.title_id & 0xFFF for p in n.packages if p.kind == nup.ADDON and p.romfs != nup.NONE}
    linked = 0
    for v in rows.get('aoc', []):
        idx, _, _rel = v.partition('\t')
        if idx.isdigit() and int(idx) in have:
            os.symlink(str(nup_path), str(aoc / f'{int(idx)}.nup')); linked += 1
    out.append(f'DM_AOC_DIR={aoc}')
    out.append(f'PNL_ADDONS={linked}')
    # the generic host's runtime profile (android/app/build.gradle genProfiles, GenericProfile.envLines): profile.bin (a modded build's own beside the pack wins), the
    # prewarm seed, the program pairs (16 bytes a row) and the profile's program list
    prof = data / 'profiles'; prof.mkdir(parents=True, exist_ok=True)
    import gen_runtime_profile as g
    pbin = pack_dir / 'profile.bin'
    if not pbin.is_file():
        pbin = prof / f'{tid}.profile.bin'
        sys.argv = ['gen_runtime_profile.py', 'profile', tid, str(pbin)]
        try: g.main()
        except SystemExit as e:
            if e.code not in (None, 0): fail(f'no runtime profile for {tid} (gen_runtime_profile.py: {e.code})')
    seed = prof / f'{tid}.seed'
    src = ENGINE / 'dm' / 'games' / tid / 'vk_prewarm.bin'
    seed.write_bytes(src.read_bytes() if src.is_file() else b'')
    pairs, seen = bytearray(), set()
    for t in ('shader_pairs.tsv', 'pso_pairs.tsv'):
        f = ENGINE / 'dm' / 'games' / tid / t
        if not f.is_file(): continue
        for l in f.read_text(encoding='utf-8', errors='replace').splitlines():
            c = l.strip().split()
            if l.startswith('#') or len(c) < 2: continue
            if all(len(x) == 16 and all(ch in '0123456789abcdefABCDEF' for ch in x) for x in c[:2]) and (c[0] + c[1]).lower() not in seen:
                seen.add((c[0] + c[1]).lower()); pairs += bytes.fromhex(c[0] + c[1])
    pf = prof / f'{tid}.pairs'; pf.write_bytes(bytes(pairs))
    progs = ENGINE / 'dm' / 'games' / tid / 'vk_programs.bin'
    pg = prof / f'{tid}.programs'
    if progs.is_file() and progs.stat().st_size % 64 == 0: pg.write_bytes(progs.read_bytes())
    elif pg.exists(): pg.unlink()
    out += [f'DM_PROFILE_PATH={pbin}', f'DM_PROFILE_SEED={seed if seed.stat().st_size else ""}',
            f'DM_PROFILE_PAIRS={pf if pairs else ""}', f'DM_PROFILE_PROGRAMS={pg if pg.is_file() else ""}']
    print('\n'.join(out))


def probe_nup(path):
    import nup
    n = nup.Nup(path)
    base = next((p for p in n.packages if p.kind == nup.BASE), None)
    upd = max((p for p in n.packages if p.kind == nup.UPDATE), key=lambda p: p.version, default=None)
    main = base or upd or n.packages[0]
    app = main.title_id & ~0xFFF if main.kind != nup.ADDON else (main.title_id - 0x1000) & ~0xFFF
    print(f'title {main.title_id:016X}')
    print(f'app {app:016X}')
    print(f'version {upd.version if upd else (base.version if base else 0)}')
    print(f'addons {sum(1 for p in n.packages if p.kind == nup.ADDON)}')
    name = Path(path).name
    for tag in ('[', '.nup'):
        k = name.find(tag)
        if k > 0: name = name[:k]
    print(f'name {name.strip()}')


def catalog(src, dst):
    d = json.loads(Path(src).read_text(encoding='utf-8'))
    lines = []
    for gm in d.get('games', []):
        builds = ','.join(f'{b.get("display") or b.get("number")}={b.get("number")}' for b in gm.get('builds', []))
        name = gm.get('name', '').replace('\t', ' ')
        lines.append('\t'.join([gm['title_id'].upper(), name, builds, '1' if gm.get('companion') else '0']))
    Path(dst).write_text('# title id\tname\tbuilds (display=version number)\tcompanion\n' + '\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} games')


def import_mod(tid, src, dest_root, name=None):
    import re, shutil, tempfile, zipfile
    tid, src, dest_root = tid.upper(), Path(src), Path(dest_root)
    kinds_wanted = ('romfs', 'exefs', 'exefs_patches', 'cheats')
    with tempfile.TemporaryDirectory(dir=str(dest_root.parent) if dest_root.parent.is_dir() else None) as tmp:
        root = src
        if src.is_file():
            if not zipfile.is_zipfile(src): fail('this file is not a .zip; unpack the mod and pick its folder instead (.7z and .rar are not read)')
            with zipfile.ZipFile(src) as z:
                for i in z.infolist():
                    n = i.filename
                    if n.startswith('/') or '\\' in n or any(part == '..' for part in n.split('/')): fail(f'unsafe path in the zip: {n}')
                    if (i.external_attr >> 16) & 0o170000 == 0o120000: fail(f'the zip holds a link: {n}')
                z.extractall(tmp)
            root = Path(tmp)
        elif not src.is_dir(): fail(f'{src} is not there')
        found = {}
        for dirpath, dirnames, _ in os.walk(root):
            depth = len(Path(dirpath).relative_to(root).parts)
            if depth > 7: dirnames[:] = []; continue
            for d in list(dirnames):
                k = d.lower()
                if k in kinds_wanted and k not in found:
                    p = Path(dirpath) / d
                    parts = [x.upper() for x in p.relative_to(root).parts]
                    other = [x for x in parts if re.fullmatch(r'0100[0-9A-F]{12}', x) and x != tid]
                    if other: fail(f'this mod is for another game ({other[0]}), not {tid}')
                    found[k] = p
            dirnames[:] = [d for d in dirnames if d.lower() not in kinds_wanted]
        if not found:
            fail('no romfs, exefs or cheats folder in it. A mod has one of these, for example MyMod/romfs/... or atmosphere/contents/' + tid + '/romfs/...')
        if not name:
            name = src.stem if src.is_file() else src.name
            if name.lower() in ('romfs', 'exefs', 'contents', 'atmosphere', tid.lower()): name = 'Mod'
        name = re.sub(r'[^A-Za-z0-9 ._()+-]', '_', name).strip(' .') or 'Mod'
        dest_root.mkdir(parents=True, exist_ok=True)
        dst = dest_root / name; i = 2
        while dst.exists(): dst = dest_root / f'{name} ({i})'; i += 1
        part = dest_root / (dst.name + '.partial')
        shutil.rmtree(part, ignore_errors=True); part.mkdir()
        for k, p in found.items(): shutil.copytree(p, part / k, symlinks=False)
        part.rename(dst)
    print(f'name={dst.name}')
    print('kinds=' + ','.join(k for k in kinds_wanted if k in found))


def extract(nup_path, inner, out):
    import nup
    n = nup.Nup(str(nup_path))
    try: k = n.node(inner)
    except FileNotFoundError as e: fail(f'{inner}: not in {Path(nup_path).name} ({e.strerror})')
    if n.is_dir(k): fail(f'{inner} is a folder')
    with n.open(k) as f, open(out, 'wb') as o:
        while True:
            b = f.read(1 << 20)
            if not b: break
            o.write(b)
    print(f'wrote {out} ({Path(out).stat().st_size} bytes)')


def pchtxt(src, out_dir):
    import re
    bid, shift, enabled, le, recs = None, 0, False, False, []
    for n, raw in enumerate(Path(src).read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'): continue
        low = line.lower()
        if low.startswith('@nsobid-'): bid = re.sub(r'[^0-9a-fA-F]', '', line[8:]).upper(); continue
        if low.startswith('@flag'):
            f = line.split()
            if len(f) >= 3 and f[1].lower() == 'offset_shift': shift = int(f[2], 0)
            continue
        if low.startswith('@little-endian'): le = True; continue
        if low.startswith('@big-endian'): le = False; continue
        if low.startswith('@enabled'): enabled = True; continue
        if low.startswith('@disabled') or low.startswith('@stop'): enabled = False; continue
        if line.startswith('@') or line.startswith('//'): continue
        if not enabled: continue
        m = re.match(r'([0-9A-Fa-f]{1,8})\s+("(?:[^"\\]|\\.)*"|[0-9A-Fa-f]+)', line)
        if not m: fail(f'{src}:{n}: not a patch line: {raw}')
        off = int(m.group(1), 16) + shift
        v = m.group(2)
        if v.startswith('"'):
            data = bytes(v[1:-1], 'utf-8').decode('unicode_escape').encode('latin-1')
        else:
            if len(v) % 2: fail(f'{src}:{n}: odd number of hex digits: {v}')
            data = bytes.fromhex(v)
            if le: data = data[::-1]
        recs.append((off, data))
    if not bid: fail(f'{src}: no @nsobid- line (which module build it is for)')
    out = Path(out_dir); out.mkdir(parents=True, exist_ok=True)
    body = bytearray(b'IPS32')
    for off, data in recs:
        for i in range(0, len(data), 0xFFFF):
            chunk = data[i:i + 0xFFFF]
            body += struct.pack('>IH', off + i, len(chunk)) + chunk
    body += b'EEOF'
    dst = out / f'{bid}.ips'   # one folder per .pchtxt: Atmosphère's rule matches the file by its name (the build ID) alone
    dst.write_bytes(bytes(body))
    print(f'ips={dst} records={len(recs)} build={bid}')


def import_dsmod(zpath, dest_root):
    import re, shutil, zipfile
    dest_root = Path(dest_root)
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        if len(names) > 20000: fail('this package has too many files')
        total = 0
        for i in z.infolist():
            n = i.filename
            if n.startswith('/') or '\\' in n or any(part == '..' for part in n.split('/')): fail(f'unsafe path in the package: {n}')
            if (i.external_attr >> 16) & 0o170000 == 0o120000: fail(f'the package holds a link: {n}')
            total += i.file_size
        if total > 2 << 30: fail('this package is too large')
        if 'package.json' not in names or 'dualscreen/manifest.json' not in names:
            fail('this is not an Eden Duo companion package (no package.json and dualscreen/manifest.json)')
        pj = json.loads(z.read('package.json').decode('utf-8'))
        if pj.get('type') != 'dual-screen-mod': fail('package.json is not a dual-screen package')
        tid = str(pj.get('title_id', '')).upper()
        if not re.fullmatch(r'[0-9A-F]{16}', tid): fail('the package has no valid title ID')
        man = json.loads(z.read('dualscreen/manifest.json').decode('utf-8'))
        if str(man.get('title_id', tid)).upper() != tid: fail('the package and its manifest name different games')
        if pj.get('requires_module') and 'linux-aarch64' not in (man.get('module') or {}).get('libraries', {}):
            fail('this package needs a native reader module and has none for Linux on ARM (linux-aarch64); '
                 'the companions bundled with Peanuts Native have one')
        dest_root.mkdir(parents=True, exist_ok=True)
        tmp, dst, old = dest_root / (tid + '.new'), dest_root / tid, dest_root / (tid + '.old')
        shutil.rmtree(tmp, ignore_errors=True); shutil.rmtree(old, ignore_errors=True)
        z.extractall(tmp)
    if dst.exists(): dst.rename(old)
    tmp.rename(dst)
    shutil.rmtree(old, ignore_errors=True)
    print(f'title_id={tid}')
    print(f'name={pj.get("name", "")}')


def main():
    a = sys.argv[1:]
    if len(a) == 4 and a[0] == 'launch': return launch(a[1], a[2], a[3])
    if len(a) == 2 and a[0] == 'probe-nup': return probe_nup(a[1])
    if len(a) == 3 and a[0] == 'catalog': return catalog(a[1], a[2])
    if len(a) == 3 and a[0] == 'import-dsmod': return import_dsmod(a[1], a[2])
    if len(a) == 4 and a[0] == 'extract': return extract(a[1], a[2], a[3])
    if len(a) == 3 and a[0] == 'pchtxt': return pchtxt(a[1], a[2])
    if len(a) in (4, 5) and a[0] == 'import-mod': return import_mod(a[1], a[2], a[3], a[4] if len(a) == 5 else None)
    sys.exit(__doc__)


if __name__ == '__main__':
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:   # the launcher shows the line
        fail(f'{type(e).__name__}: {e}')
