#!/usr/bin/env python3
"""Peanuts Native for Linux: the Eden Duo companion packages for the AppImage, with linux-aarch64 reader modules.

  build_companions.py <eden-duo-companions checkout> <built modules folder> <out folder>

Every package the companions repository releases (tools/build_release.sh: its `pkg` lines give each package's version, minimum runtime and
build IDs) is made with the repository's own packager (tools/build_dualscreen_package.py) and unpacked into <out>/<title id>/, the layout the
launcher copies next to a game's build. The reader modules are the repository's native/modules sources built for Linux on ARM; the packager
puts them in as "linux-aarch64" (the platform the companion runtime in peanuts-run asks for). A package whose module did not build, or that
needs a newer companion runtime than this one, is left out and listed.
"""
import json, os, re, shlex, shutil, subprocess, sys, tempfile, zipfile
from pathlib import Path

RUNTIME_VERSION = 25   # dm/peanuts/rt/mods/mod_runtime.h DualScreenRuntimeVersion
PLATFORM = 'linux-aarch64'


def release_lines(script: Path):
    """The `pkg` invocations of build_release.sh, continuation lines joined."""
    text = script.read_text(encoding='utf-8').replace('\\\n', ' ')
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('pkg ') and not s.startswith('pkg()'):
            yield s


def build_ids(pkg_dir: Path):
    m = json.loads((pkg_dir / 'dualscreen' / 'manifest.json').read_text(encoding='utf-8'))
    ids = (m.get('module') or {}).get('build_ids')
    if not ids and (pkg_dir / 'package.json').is_file():
        ids = (json.loads((pkg_dir / 'package.json').read_text(encoding='utf-8')).get('module') or {}).get('build_ids')
    return ids or []


def main():
    repo, mods, out = (Path(a).resolve() for a in sys.argv[1:4])
    sys.path.insert(0, str(repo / 'tools'))
    import build_dualscreen_package as bdp
    bdp.PLATFORMS = set(bdp.PLATFORMS) | {PLATFORM}
    strip = os.environ.get('STRIP') or shutil.which('llvm-strip') or shutil.which('aarch64-linux-gnu-strip') or shutil.which('strip')
    out.mkdir(parents=True, exist_ok=True)
    jobs = {}
    for line in release_lines(repo / 'tools' / 'build_release.sh'):
        line = re.sub(r'"\$\{[A-Z0-9_]+:-([^}]*)\}"', r'\1', line)
        line = re.sub(r'\$\(build_ids ([A-Za-z0-9]+)\)', lambda m: ' '.join(f'--build-id {b}' for b in build_ids(repo / 'packages' / m.group(1))), line)
        a = shlex.split(line)[1:]
        d, version, rest = a[0], a[2], a[3:]
        args, has_module, i = {'build_ids': [], 'min_runtime': None, 'title_id': None, 'name': None}, False, 0
        while i < len(rest):
            k = rest[i]; v = rest[i + 1] if i + 1 < len(rest) else None
            if k == '--module': has_module = True
            elif k == '--build-id': args['build_ids'].append(v)
            elif k == '--min-runtime': args['min_runtime'] = int(v)
            elif k == '--title-id': args['title_id'] = v
            elif k == '--name': args['name'] = v
            else: raise SystemExit(f'{d}: unknown packager argument {k}')
            i += 2
        jobs[d] = (version, has_module, args)
    # packages the release script does not list but that carry their own package.json
    for p in sorted((repo / 'packages').iterdir()):
        if p.name in jobs or not (p / 'package.json').is_file(): continue
        pj = json.loads((p / 'package.json').read_text(encoding='utf-8'))
        jobs[p.name] = (pj.get('version', '1.0.0'), bool(pj.get('requires_module') or pj.get('module')),
                        {'build_ids': (pj.get('module') or {}).get('build_ids', []), 'min_runtime': pj.get('min_runtime'), 'title_id': pj.get('title_id'), 'name': pj.get('name')})
    made, skipped = [], []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for d, (version, has_module, args) in sorted(jobs.items()):
            src = repo / 'packages' / d
            man = json.loads((src / 'dualscreen' / 'manifest.json').read_text(encoding='utf-8'))
            tid = (args['title_id'] or man.get('title_id') or '').upper()
            if args['min_runtime'] and args['min_runtime'] > RUNTIME_VERSION:
                skipped.append(f'{d}: needs companion runtime {args["min_runtime"]}'); continue
            modules = []
            if has_module or man.get('requires_module'):
                so = mods / f'{tid}.so'
                if not so.is_file():
                    skipped.append(f'{d}: its reader module ({tid}.so) did not build'); continue
                stripped = tmp / 'so' / f'{tid}.so'; stripped.parent.mkdir(exist_ok=True)
                shutil.copyfile(so, stripped)
                subprocess.run([strip, '--strip-all', str(stripped)], check=True)
                modules = [f'{PLATFORM}={stripped}']
            try:
                z = bdp.build_package(src, tmp / 'zips', version, name=args['name'], override_title_id=args['title_id'], modules=modules,
                                      module_build_ids=args['build_ids'] or None, min_runtime=args['min_runtime'])
            except bdp.PackageError as e:
                skipped.append(f'{d}: {e}'); continue
            dst = out / tid
            shutil.rmtree(dst, ignore_errors=True)
            with zipfile.ZipFile(z) as zf: zf.extractall(dst)
            pj = json.loads((dst / 'package.json').read_text(encoding='utf-8'))
            made.append(f'{tid}  {pj["name"]} {pj["version"]}' + ('  (module)' if modules else ''))
    print(f'{len(made)} companion packages:'); print('\n'.join('  ' + m for m in made))
    if skipped: print(f'{len(skipped)} left out:'); print('\n'.join('  ' + s for s in skipped))


if __name__ == '__main__':
    main()
