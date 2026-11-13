import pathlib, subprocess, os
workspace=pathlib.Path(__file__).resolve().parents[3]
base=workspace/"dev/steamdeck"
zig=next((base/'zig').glob('*/zig.exe')).as_posix()
for name,driver in [('cc','cc'),('cxx','c++')]:
    flags=''
    if name=='cxx':flags=' -nostdinc++ -nostdlib++ "-I'+(base/'sysroot/usr/include/c++/12').as_posix()+'" "-I'+(base/'sysroot/usr/include/x86_64-linux-gnu/c++/12').as_posix()+'"'
    (base/(name+'.cmd')).write_text('@echo off\nset "ZIG_GLOBAL_CACHE_DIR='+(base/'cache/global').as_posix()+'"\nset "ZIG_LOCAL_CACHE_DIR='+(base/'cache/local').as_posix()+'"\n"'+zig+'" '+driver+' -target x86_64-linux-gnu.2.36'+flags+' %*\n')
root=(base/'sysroot').as_posix()
(base/'toolchain.cmake').write_text('''set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "'''+root+'''")
set(ENV{PKG_CONFIG_LIBDIR} "'''+root+'''/usr/lib/x86_64-linux-gnu/pkgconfig;'''+root+'''/usr/share/pkgconfig")
set(CMAKE_C_COMPILER "'''+(base/'cc.cmd').as_posix()+'''")
set(CMAKE_CXX_COMPILER "'''+(base/'cxx.cmd').as_posix()+'''")
set(CMAKE_FIND_ROOT_PATH "'''+root+'''")
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
set(CMAKE_LIBRARY_PATH "'''+root+'''/usr/lib/x86_64-linux-gnu;'''+root+'''/lib/x86_64-linux-gnu")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "'''+root+'''/usr/include;'''+root+'''/usr/include/x86_64-linux-gnu")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L'''+root+'''/usr/lib/x86_64-linux-gnu -Wl,-rpath,\\$ORIGIN/lib")
set(CMAKE_AR "'''+zig+'''")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")
''')
visual_studio=pathlib.Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio'
cmake=next(visual_studio.glob('*/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'))
ninja=next(visual_studio.glob('*/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'))
command=[str(cmake),'-S',str(workspace/'project/lifeline-linux'),'-B',str(workspace/'dev/build/linux'),'-G','Ninja',
    '-DCMAKE_TOOLCHAIN_FILE='+str(base/'toolchain.cmake'),'-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_BUILD_TYPE=Release',
    '-DFETCHCONTENT_SOURCE_DIR_SDL3='+str(workspace/'dev/cmake/_deps/sdl3-src'),
    '-DLIFELINE_DXC='+str(base/'dxc/bin/x64/dxc.exe'),'-DLIFELINE_OPENGL=ON','-DLIFELINE_STEAMDECK=ON',
    '-DLIFELINE_SPIRV_CROSS='+str(base/'spirv-cross-build/spirv-cross.exe'),'-U','HAVE_OPENGL*',
    '-DSDL_X11=ON','-DSDL_WAYLAND=OFF','-DSDL_ALSA=ON','-DSDL_PULSEAUDIO=ON','-DSDL_PIPEWIRE=OFF',
    '-DSDL_KMSDRM=OFF','-DSDL_OPENGL=ON','-DSDL_OPENGLES=OFF','-DSDL_HIDAPI_LIBUSB=OFF',
    '-DSDL_LIBURING=OFF','-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON',
    '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib','-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON']
command += ['-DCMAKE_EXE_LINKER_FLAGS=-L'+root+'/usr/lib/x86_64-linux-gnu -Wl,-rpath,$ORIGIN/lib',
    '-DCMAKE_CXX_STANDARD_LIBRARIES='+root+'/usr/lib/x86_64-linux-gnu/libstdc++.so.6 '+root+'/lib/x86_64-linux-gnu/libgcc_s.so.1 -lm',
    '-DPKG_CONFIG_EXECUTABLE='+str(base/'pkgconf/pkgconf.exe')]
os.environ['PKG_CONFIG_SYSROOT_DIR']=root
os.environ['PKG_CONFIG_LIBDIR']=root+'/usr/lib/x86_64-linux-gnu/pkgconfig;'+root+'/usr/share/pkgconfig'
subprocess.run(command,check=True)
