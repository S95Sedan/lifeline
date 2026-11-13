# Lifeline installer

A native Windows installer with a choice of Windows installation or a complete
portable Linux/Steam Deck folder. It includes both native runtimes and one
conversion recipe. It reads the player's **SLUS_208.48 Lifeline ISO** and converts
its resources and movies into the chosen folder.

The wizard starts with two selectable cards on a **Choose a version** page:
**Windows installation** and **Steam Deck portable folder**. Disc selection,
destination and the installation summary follow on separate pages.

Run [build-installer.bat](../../build-installer.bat) from the repository root.
The clean build prepares the native Linux runtime and its verified libraries
automatically, compiles the Windows game and installer, and produces
**`build/Installer.exe`**. Distribute that executable on its own.

No prebuilt Steam Deck folder, WSL, VM or separately installed Python is needed.
For an optional prebuilt runtime override, set `LIFELINE_LINUX_RUNTIME` in CMake;
see the [Linux build guide](../lifeline-linux/README.md).

The ISO is required when installing, rather than while building. Movie
conversion uses static FFmpeg libraries and Windows Media Foundation.
Windows settings and saves live in `user/` beside the installed game. Copy the
entire portable output to the Deck and run `sh ./launch.sh`; no Proton is used.

See [building](../../README.md#build), [controls](../../README.md#controls),
and [third-party components](../../README.md#credits).
