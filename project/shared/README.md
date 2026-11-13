# Shared Lifeline runtime

Platform-independent C++20 game systems, interfaces, SDL input/audio/UI,
software rendering and the shader shared by both hardware renderers. Speech
recognition uses the player's installed game models and runs locally.

Configure `../lifeline-windows/` or `../lifeline-linux/`, which selects the
matching platform implementations. This folder is included once by either
build; it is not a standalone CMake entry point.

Use the repository's [installer build](../../README.md#build) to build and
install the game. The executable expects its installed `data/` folder beside
it, with settings and saves in `user/`.

Read the [controls](../../README.md#controls), [settings](../../README.md#settings),
and [third-party notices](../../README.md#credits).
