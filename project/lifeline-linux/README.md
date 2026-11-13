# Native Linux and Steam Deck runtime

This is the Linux CMake entry point. Common game code comes from `../shared/`.
This folder owns OpenGL, FFmpeg decoding, POSIX file/time services, Linux
display/default profiles and the Steam Deck launcher/packager. It does not use
Proton or include Windows rendering/media implementations.

## Building the installer on Windows

Run the repository's `build-installer.bat`. Its default CMake build prepares the
native Linux runtime automatically, alongside the Windows game. Hash-pinned
Zig, shader tools and Linux header/library packages are downloaded into `dev/`.
Python is obtained as a portable build tool if no suitable interpreter exists.
No installed game data, WSL, VM or prebuilt Deck executable is required to build
the installer. The user's ISO is selected later when installing either target.

`LIFELINE_LINUX_RUNTIME` is an optional CMake override for a verified runtime
folder. Leaving it empty builds from source. The default payload is assembled
under `dev/installer-native/linux-runtime/`; it contains no game data and is not
itself a playable installation. Keep caches, compiler tools and sysroots in `dev/`.

## Native Linux build

Use a C++20 compiler, CMake, Ninja, Python 3, DXC with SPIR-V support,
SPIRV-Cross, SDL's X11/audio development dependencies and FFmpeg development
headers/libraries. Pass already installed game data to assemble a playable folder.

On Linux with the documented build dependencies:

```sh
sh project/lifeline-linux/packaging/steamdeck/build.sh build/Install/data
```

The build cache is `dev/build/linux/` and the complete portable output is
`build/Steamdeck/`. Steam Deck uses fullscreen only, 1280x800 defaults, OpenGL
and a maximum render scale of 100%. Its resolution selector changes internal
rendering size without switching display modes. Old windowed/borderless
settings are accepted and normalized to fullscreen. Set `LIFELINE_STEAMDECK=OFF` for desktop Linux
defaults when configuring CMake directly.

## Existing development toolchain

For development with an already prepared Windows cross-toolchain, the helper uses the
compiler/sysroot tools in `dev/steamdeck/`:

```bat
python project/lifeline-linux/tools/configure_windows.py
cmake --build dev/build/linux --target lifeline
python project/lifeline-linux/packaging/steamdeck/package.py --binary dev/build/linux/bin/lifeline --assets build/Install/data --output build/Steamdeck --sysroot dev/steamdeck/sysroot
```

For an installer runtime payload without a duplicate game-data tree, pass
`--runtime-only` instead of `--assets`. The installer converts the shared data
directly from the user's ISO. See `packaging/steamdeck/README.txt` for transfer,
Steam Input and first-launch instructions. No archive is required.
