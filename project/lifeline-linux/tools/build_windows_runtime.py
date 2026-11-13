"""Build the native Deck installer payload on Windows, without game assets.

All downloaded tools, Linux headers/libraries and build caches stay in dev/.
No Linux installation, VM, WSL, administrator rights or system Python is needed.
"""
import argparse
import concurrent.futures
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            value.update(block)
    return value.hexdigest()


def inside(root, relative):
    relative = str(relative).replace('\\', '/')
    if relative.startswith('/') or ':' in relative:
        raise ValueError('Absolute archive path: '+relative)
    path = (root/relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError('Archive path escapes output: '+relative)
    return path


def download(record, cache):
    path = inside(cache, record['file'])
    if path.is_file() and digest(path) == record['sha256']:
        return path
    print('Downloading '+record['file'], flush=True)
    temporary = path.with_name(path.name+'.part')
    request = urllib.request.Request(record['url'], headers={'User-Agent': 'Lifeline-source-build'})
    with urllib.request.urlopen(request, timeout=120) as source, temporary.open('wb') as output:
        shutil.copyfileobj(source, output)
    if digest(temporary) != record['sha256']:
        raise RuntimeError('SHA256 mismatch: '+record['file'])
    temporary.replace(path)
    return path


def unpack_zip(path, folder):
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            target = inside(folder, entry.filename)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)


def unpack_msi(path, folder):
    """Read embedded cabinets without installing the MSI or requiring admin."""
    folder.mkdir(parents=True, exist_ok=True)
    msi = ctypes.WinDLL('msi')
    handle = ctypes.c_uint
    pointer = ctypes.POINTER(handle)
    msi.MsiOpenDatabaseW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, pointer]
    msi.MsiDatabaseOpenViewW.argtypes = [handle, ctypes.c_wchar_p, pointer]
    msi.MsiViewExecute.argtypes = [handle, handle]
    msi.MsiViewFetch.argtypes = [handle, pointer]
    msi.MsiRecordReadStream.argtypes = [handle, ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
    msi.MsiCloseHandle.argtypes = [handle]
    database, view = handle(), handle()
    def checked(code):
        if code:
            raise RuntimeError('Cannot read pkgconf MSI: Windows Installer error '+str(code))
    checked(msi.MsiOpenDatabaseW(str(path), None, ctypes.byref(database)))
    try:
        checked(msi.MsiDatabaseOpenViewW(database, 'SELECT `Data` FROM `_Streams`', ctypes.byref(view)))
        checked(msi.MsiViewExecute(view, 0))
        number = 0
        while True:
            record = handle()
            status = msi.MsiViewFetch(view, ctypes.byref(record))
            if status == 259:
                break
            checked(status)
            try:
                data = bytearray()
                while True:
                    block = ctypes.create_string_buffer(65536)
                    count = ctypes.c_uint(len(block))
                    checked(msi.MsiRecordReadStream(record, 1, block, ctypes.byref(count)))
                    data.extend(block.raw[:count.value])
                    if not count.value:
                        break
                if data[:4] == b'MSCF':
                    cabinet = folder/('payload'+str(number)+'.cab')
                    cabinet.write_bytes(data)
                    subprocess.run(['expand.exe', '-F:*', str(cabinet), str(folder)], check=True)
                    number += 1
            finally:
                msi.MsiCloseHandle(record)
    finally:
        if view.value:
            msi.MsiCloseHandle(view)
        msi.MsiCloseHandle(database)
    source = next((p for p in folder.rglob('*') if p.name.lower() in {'pkgconfexefile', 'pkgconf.exe'} and p.is_file()), None)
    if source is None:
        raise RuntimeError('The pkgconf archive did not contain its executable')
    if source != folder/'pkgconf.exe':
        shutil.copy2(source, folder/'pkgconf.exe')


def prepare_tool(name, record, cache, base):
    folder = base/name
    marker = folder/'.source-pin'
    sentinel = folder/{'zig': 'zig-x86_64-windows-0.14.1/zig.exe', 'dxc': 'bin/x64/dxc.exe', 'pkgconf': 'pkgconf.exe', 'spirv-cross': 'SPIRV-Cross-vulkan-sdk-1.3.296.0/CMakeLists.txt'}[name]
    if sentinel.is_file() and marker.is_file() and marker.read_text() == record['sha256']:
        return folder
    archive = download(record, cache)
    if name == 'pkgconf':
        unpack_msi(archive, folder)
    else:
        unpack_zip(archive, folder)
    marker.write_text(record['sha256'])
    return folder


def prepare_sysroot(records, cache, base):
    root = base/'sysroot'
    root.mkdir(exist_ok=True)
    signature = hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()
    marker = root/'.source-pin'
    if marker.is_file() and marker.read_text() == signature:
        return root
    print('Preparing Linux headers and libraries ('+str(len(records))+' verified packages)', flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        paths = list(pool.map(lambda record: download(record, cache), records))
    links = []
    for path in paths:
        raw = path.read_bytes()
        if raw[:8] != b'!<arch>\n':
            raise RuntimeError('Invalid Debian package: '+path.name)
        offset = 8
        while offset+60 <= len(raw):
            name = raw[offset:offset+16].decode().strip().rstrip('/')
            size = int(raw[offset+48:offset+58])
            data = raw[offset+60:offset+60+size]
            offset += 60+size+(size%2)
            if not name.startswith('data.tar'):
                continue
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                for entry in archive:
                    target = inside(root, entry.name)
                    if entry.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    elif entry.isfile():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with archive.extractfile(entry) as source, target.open('wb') as output:
                            shutil.copyfileobj(source, output)
                    elif entry.issym() or entry.islnk():
                        relative = entry.linkname.lstrip('/') if entry.linkname.startswith('/') or entry.islnk() else str(target.parent.relative_to(root)/entry.linkname)
                        links.append((target, inside(root, relative)))
            break
        else:
            raise RuntimeError('No data archive in '+path.name)
    # Materialize links as files: ordinary Windows users need no symlink privilege.
    for _ in range(8):
        pending = []
        for target, source in links:
            if source.is_file():
                if target != source:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
            else:
                pending.append((target, source))
        links = pending
    marker.write_text(signature)
    return root


def run(*args, env=None):
    subprocess.run([str(arg) for arg in args], check=True, env=env)


def write_if_changed(path, text):
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


def toolchain(base, zig, sysroot):
    root = sysroot.as_posix()
    for name, driver in [('cc', 'cc'), ('cxx', 'c++')]:
        # Keep libstdc++ before Zig's libc headers: its include_next needs them.
        flags = '' if name == 'cc' else ' -nostdinc++ -nostdlib++ '+f'"-I{root}/usr/include/c++/12" "-I{root}/usr/include/x86_64-linux-gnu/c++/12"'
        write_if_changed(base/(name+'.cmd'), '@echo off\n'+f'set "ZIG_GLOBAL_CACHE_DIR={(base/"cache/global").as_posix()}"\nset "ZIG_LOCAL_CACHE_DIR={(base/"cache/local").as_posix()}"\n"{zig.as_posix()}" {driver} -target x86_64-linux-gnu.2.36{flags} %*\n')
    path = base/'toolchain.cmake'
    write_if_changed(path, f'''set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(ENV{{PKG_CONFIG_SYSROOT_DIR}} "{root}")
set(ENV{{PKG_CONFIG_LIBDIR}} "{root}/usr/lib/x86_64-linux-gnu/pkgconfig;{root}/usr/share/pkgconfig")
set(CMAKE_C_COMPILER "{(base/'cc.cmd').as_posix()}")
set(CMAKE_CXX_COMPILER "{(base/'cxx.cmd').as_posix()}")
set(CMAKE_FIND_ROOT_PATH "{root}")
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
set(CMAKE_LIBRARY_PATH "{root}/usr/lib/x86_64-linux-gnu;{root}/lib/x86_64-linux-gnu")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# These are SDK headers, including when an older cache still has them in -I.
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "{root}/usr/include;{root}/usr/include/x86_64-linux-gnu")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${{CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}}")
set(CMAKE_EXE_LINKER_FLAGS_INIT [==["-L{root}/usr/lib/x86_64-linux-gnu" "-L{root}/lib/x86_64-linux-gnu"]==])
set(CMAKE_CXX_STANDARD_LIBRARIES [==["{root}/usr/lib/x86_64-linux-gnu/libstdc++.so.6" "{root}/lib/x86_64-linux-gnu/libgcc_s.so.1" -lm]==])
set(CMAKE_AR "{zig.as_posix()}")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")
''')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--cmake', default=shutil.which('cmake'))
    parser.add_argument('--ninja', default=shutil.which('ninja'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt' or not args.cmake or not args.ninja or args.jobs < 1:
        parser.error('Use an x64 Visual Studio developer environment with CMake and Ninja')
    workspace = args.workspace.resolve()
    base = workspace/'dev/steamdeck'
    base.mkdir(parents=True, exist_ok=True)
    cache = Path(os.environ.get('LIFELINE_LINUX_DOWNLOAD_CACHE', str(base/'downloads'))).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads(Path(__file__).with_name('dependencies.json').read_text())
    tools = lock['tools']
    for name in ('zig', 'dxc', 'pkgconf', 'spirv-cross'):
        prepare_tool(name, tools[name], cache, base)
    sysroot = prepare_sysroot(lock['packages'], cache, base)
    zig = base/'zig/zig-x86_64-windows-0.14.1/zig.exe'
    cross_source = base/'spirv-cross/SPIRV-Cross-vulkan-sdk-1.3.296.0'
    cross_build = base/'spirv-cross-build'
    run(args.cmake, '-S', cross_source, '-B', cross_build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MAKE_PROGRAM='+args.ninja,
        '-DSPIRV_CROSS_ENABLE_TESTS=OFF', '-DSPIRV_CROSS_ENABLE_MSL=ON', '-DSPIRV_CROSS_ENABLE_HLSL=ON', '-DSPIRV_CROSS_ENABLE_CPP=ON', '-DSPIRV_CROSS_ENABLE_C_API=OFF')
    run(args.cmake, '--build', cross_build, '--target', 'spirv-cross', '--parallel', args.jobs)
    if args.prepare_only:
        return
    build = workspace/'dev/build/linux'
    command = [args.cmake, '-S', workspace/'project/lifeline-linux', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_MAKE_PROGRAM='+args.ninja, '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain(base, zig, sysroot)), '-DLIFELINE_BUILD_TOOLS=OFF',
        '-DLIFELINE_DXC='+str(base/'dxc/bin/x64/dxc.exe'), '-DLIFELINE_SPIRV_CROSS='+str(cross_build/'spirv-cross.exe'),
        '-DPKG_CONFIG_EXECUTABLE='+str(base/'pkgconf/pkgconf.exe'), '-DLIFELINE_OPENGL=ON', '-DLIFELINE_STEAMDECK=ON',
        '-DSDL_X11=ON', '-DSDL_WAYLAND=OFF', '-DSDL_ALSA=ON', '-DSDL_PULSEAUDIO=ON', '-DSDL_PIPEWIRE=OFF', '-DSDL_KMSDRM=OFF',
        '-DSDL_OPENGL=ON', '-DSDL_OPENGLES=OFF', '-DSDL_HIDAPI_LIBUSB=OFF', '-DSDL_LIBURING=OFF',
        '-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON', '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib', '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON']
    env = os.environ.copy()
    env['PKG_CONFIG_SYSROOT_DIR'] = sysroot.as_posix()
    env['PKG_CONFIG_LIBDIR'] = ';'.join(str(sysroot/p) for p in ('usr/lib/x86_64-linux-gnu/pkgconfig', 'usr/share/pkgconfig'))
    run(*command, env=env)
    run(args.cmake, '--build', build, '--target', 'lifeline', '--parallel', args.jobs, env=env)
    run(sys.executable, '-B', workspace/'project/lifeline-linux/packaging/steamdeck/package.py', '--binary', build/'bin/lifeline', '--runtime-only', '--output', args.output.resolve(), '--sysroot', sysroot)


if __name__ == '__main__':
    main()
