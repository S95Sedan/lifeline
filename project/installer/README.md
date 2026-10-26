# Lifeline installer

A native Windows installer that includes the compiled game and a conversion
recipe. It reads the player's **SLUS_208.48 Lifeline ISO**, converts its resources
and movies, and installs the game into the chosen folder.

Run [build-installer.bat](../../build-installer.bat) from the repository root.
The build compiles the game and installer together and produces
**`build/Installer.exe`**. Distribute that executable on its own.

The ISO is required when installing, rather than while building. Movie
conversion uses static FFmpeg libraries and Windows Media Foundation.
Player settings and saves live in `user/` beside the installed game.

See [building](../../README.md#build), [controls](../../README.md#controls),
and [third-party components](../../README.md#credits).
