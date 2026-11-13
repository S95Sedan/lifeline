# Lifeline projects

| Folder | Responsibility |
| --- | --- |
| `lifeline-windows/` | Windows build entry point, D3D12, Media Foundation, SAPI and Win32 host services |
| `lifeline-linux/` | Native Linux/Steam Deck build entry point, OpenGL, FFmpeg, POSIX host services and portable packaging |
| `shared/` | One copy of gameplay, assets, scripts, speech recognition, SDL UI/input, common rendering and interfaces |
| `installer/` | Windows setup application: Windows installation or complete portable Linux/Steam Deck export |

Build Windows with `build.bat` at the workspace root. Build the installer with
`build-installer.bat`. See the individual platform READMEs for Linux builds.
Caches and temporary tools live under `dev/`; runnable outputs remain under
`build/`. Original game data comes only from the user's read-only ISO.

Platform implementations are selected at build time. Shared game code calls
the interfaces in `shared/include/platform/`; it contains no Win32/POSIX
implementation branches. Both renderers compile the same scene shader from
`shared/src/rendering/shaders/scene_raster.hlsl`. Installed game assets use the
same layout and conversion recipe for both targets; they are not duplicated
inside the source tree or embedded twice in the installer.
