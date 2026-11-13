"""Assemble a runnable native Linux directory; build state stays outside it.

Can package a native Linux build or a Windows cross-build with --sysroot.
Only copies assets already installed from the user's disc; never reads sources/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tarfile


def needed(path):
    raw = path.read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01':
        raise RuntimeError(f'Expected a little-endian ELF64 binary: {path}')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', raw)
    segments = [struct.unpack_from('<IIQQQQQQ', raw, header[5]+i*header[9]) for i in range(header[10])]
    dynamic = next((p for p in segments if p[0] == 2), None)
    if dynamic is None:
        return []
    entries = []
    for offset in range(dynamic[2], dynamic[2]+dynamic[5], 16):
        entry = struct.unpack_from('<qQ', raw, offset)
        if entry[0] == 0:
            break
        entries.append(entry)
    address = next(v for k, v in entries if k == 5)
    segment = next(p for p in segments if p[0] == 1 and p[3] <= address < p[3]+p[5])
    strings = address-segment[3]+segment[2]
    return [raw[strings+v:raw.index(b'\0', strings+v)].decode() for k, v in entries if k == 1]


def make_runtime_path_transitive(path, *, library=False):
    """Let a direct ELF launch resolve the bundled libraries' dependencies.

    DT_RUNPATH only applies to direct dependencies; DT_RPATH is inherited by
    their dependencies too. Executables retain $ORIGIN/lib. Selected bundled
    libraries replace distribution-specific paths with $ORIGIN in the same
    string-table allocation, without relocating ELF data or needing patchelf.
    """
    raw = bytearray(path.read_bytes())
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', raw)
    segments = [struct.unpack_from('<IIQQQQQQ', raw, header[5]+i*header[9]) for i in range(header[10])]
    dynamic = next(p for p in segments if p[0] == 2)
    entries = []
    for offset in range(dynamic[2], dynamic[2]+dynamic[5], 16):
        kind, value = struct.unpack_from('<qQ', raw, offset)
        if kind == 0:
            break
        entries.append((offset, kind, value))
    address = next(v for _, k, v in entries if k == 5)
    segment = next(p for p in segments if p[0] == 1 and p[3] <= address < p[3]+p[5])
    strings = address-segment[3]+segment[2]
    entry = next((e for e in entries if e[1] in (15, 29)), None)
    if entry is None:
        if library:
            return
        raise RuntimeError('Build the executable with CMAKE_INSTALL_RPATH=$ORIGIN/lib and CMAKE_BUILD_WITH_INSTALL_RPATH=ON')
    offset, kind, value = entry
    search = bytes(raw[strings+value:raw.index(b'\0', strings+value)]).decode()
    changed = False
    if library and search != '$ORIGIN':
        previous_length = len(search.encode())
        if previous_length < len(b'$ORIGIN'):
            raise RuntimeError(f'No room to relocate the private-library search path: {path}')
        raw[strings+value:strings+value+previous_length+1] = b'$ORIGIN'+b'\0'*(previous_length+1-len(b'$ORIGIN'))
        changed = True
    elif not library and '$ORIGIN/lib' not in search.split(':'):
        raise RuntimeError('The executable must include $ORIGIN/lib in its runtime library search path')
    if kind == 29:
        struct.pack_into('<q', raw, offset, 15)
        changed = True
    if changed:
        path.write_bytes(raw)


def package(args):
    destination = args.output.resolve()
    if args.assets and (destination == args.assets.resolve() or destination in args.assets.resolve().parents):
        raise RuntimeError('Output must be separate from the existing installation')
    binary = args.binary.resolve()
    needed(binary)  # Reject Windows executables before writing the package.
    if not args.runtime_only and (not args.assets or not (args.assets/'manifest.json').is_file()):
        raise RuntimeError('Expected an installed game data directory containing manifest.json')
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination/'lifeline')
    make_runtime_path_transitive(destination/'lifeline')
    (destination/'lifeline').chmod(0o755)
    if not args.runtime_only:
        shutil.copytree(args.assets, destination/'data', dirs_exist_ok=True)
    library_dir = destination/'lib'
    library_dir.mkdir(exist_ok=True)
    roots = [args.sysroot/p for p in ('usr/lib/x86_64-linux-gnu', 'lib/x86_64-linux-gnu', 'usr/lib64', 'lib64', 'usr/lib', 'lib')]
    # Drivers, glibc and the C++ runtime belong to SteamOS. Bundling older
    # versions can break the user's graphics driver. Deck SteamOS supplies these.
    host = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1',
            'ld-linux-x86-64.so.2', 'libstdc++.so.6', 'libgcc_s.so.1', 'libresolv.so.2',
            'libdrm.so.2', 'libGL.so.1', 'libGLX.so.0', 'libGLdispatch.so.0'}
    seen = set()
    copied = []

    def locate(name):
        for root in roots:
            for candidate in (root/name, root/'pulseaudio'/name):
                if candidate.is_file():
                    return candidate
        raise RuntimeError(f'Missing runtime dependency: {name}')

    def include(name):
        if name in seen or name in host:
            return
        seen.add(name)
        source = locate(name)
        shutil.copy2(source, library_dir/name, follow_symlinks=True)
        if name.startswith('libpulse'):
            make_runtime_path_transitive(library_dir/name, library=True)
        copied.append({'name': name, 'sha256': hashlib.sha256((library_dir/name).read_bytes()).hexdigest()})
        for dependency in needed(source):
            include(dependency)

    for dependency in needed(binary):
        include(dependency)
    # SDL loads these dynamically. The cross-build sysroot can contain copies
    # instead of symlinks, hence include the development aliases as well.
    for name in ('libX11.so', 'libXext.so', 'libXcursor.so', 'libXi.so', 'libXfixes.so',
                 'libXrandr.so', 'libXss.so', 'libXtst.so', 'libasound.so', 'libpulse.so', 'libudev.so'):
        try:
            include(name)
        except RuntimeError:
            if args.sysroot != Path('/'):
                raise
    here = Path(__file__).resolve().parent
    for source, name in (('Launch.sh', 'launch.sh'), ('README.txt', 'README.txt')):
        shutil.copy2(here/source, destination/name)
    (destination/'launch.sh').chmod(0o755)
    licenses = destination/'licenses'
    licenses.mkdir(exist_ok=True)
    for root in (args.sysroot/'usr/share/doc', args.installed_licenses):
        if root and root.is_dir():
            if root.name == 'doc':
                for copyright_file in root.glob('*/copyright'):
                    shutil.copy2(copyright_file, licenses/(copyright_file.parent.name+'-copyright.txt'))
            else:
                shutil.copytree(root, licenses/'game-runtime', dirs_exist_ok=True)
    (destination/'runtime-manifest.json').write_text(json.dumps({
        'platform': 'Linux x86_64', 'renderer': 'OpenGL 4.3 compute',
        'default_output': [1280, 800], 'glibc_minimum': '2.36',
        'launcher_revision': 4,
        'title_menu_revision': 3,
        'postprocess_revision': 3,
        'display_revision': 2,
        'graphics_optimization_revision': 1,
        'host_libraries': sorted(host), 'bundled_libraries': sorted(copied, key=lambda x: x['name']),
        'binary_sha256': hashlib.sha256((destination/'lifeline').read_bytes()).hexdigest(),
    }, indent=2)+'\n', encoding='utf-8')
    # The archive preserves executable bits when copying through Windows or
    # filesystems that cannot store Linux permissions.
    archive = args.archive
    if archive:
        archive.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive, 'w:gz', compresslevel=3) as tar:
            for path in sorted(destination.rglob('*')):
                if path.is_file():
                    info = tar.gettarinfo(str(path), str(Path('Steamdeck')/path.relative_to(destination)))
                    info.mode = 0o755 if path.name in ('launch.sh', 'lifeline') else 0o644
                    with path.open('rb') as content:
                        tar.addfile(info, content)
    print(f'Packaged {destination} with {len(copied)} runtime libraries')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--runtime-only', action='store_true', help='Prepare installer runtime files without copying game data')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sysroot', type=Path, default=Path('/'))
    parser.add_argument('--installed-licenses', type=Path)
    parser.add_argument('--archive', type=Path)
    package(parser.parse_args())
