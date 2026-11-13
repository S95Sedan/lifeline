# Windows runtime

This is the Windows CMake entry point. It builds the common runtime from
`../shared/` and selects Windows-specific implementations from this folder:

- D3D12 device/rendering backend and generated shader wrapper.
- Media Foundation movie decoding and Windows speech integration.
- Win32 atomic file replacement, console handling, physical monitor query,
  local time, window icons and Windows display/render defaults.
- Windows resources and application entry/link settings.

From the workspace root, use `build.bat`. To build just the executable in a
Visual Studio developer terminal:

```bat
cmake -S project/lifeline-windows -B dev/build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build dev/build/windows --target lifeline
```

The default executable output is `dev/build/windows/bin/lifeline.exe`;
`build.bat` selects the existing runnable `build/Install/` destination.
Headers specific to this backend live under `include/rendering/`.
