LIFELINE - NATIVE STEAM DECK BUILD

Copy this entire Steamdeck folder to the Deck's internal storage or an ext4
microSD card. No Windows executable, Proton, disc image or installation step
is required. SteamOS with glibc 2.36+ and its AMD OpenGL driver is needed.

DESKTOP MODE
Open the Steamdeck folder itself, right-click empty space and select Open
Terminal Here. Run:
    sh ./launch.sh
The launcher repairs executable permissions if a folder copy lost them.
After that, ./launch.sh works as well.

GAME MODE
In Desktop Mode, add launch.sh to Steam as a Non-Steam Game. Select the
Gamepad controller template. Leave "Force the use of a specific Steam Play
compatibility tool" UNCHECKED. Return to Gaming Mode and launch the shortcut.
If adding the script is awkward, add /bin/sh instead and set launch options to
the quoted absolute path to launch.sh. Set Start In to this folder.
Opening launch.sh directly in Desktop Mode runs outside Steam's game launch.
Use the Library shortcut in Gaming Mode for the in-game Steam button menu.

DISPLAY AND GRAPHICS
Defaults: OpenGL hardware rendering, fullscreen, 1280 x 800, Auto aspect ratio,
100% render scale and VSync. Original movies and HUD keep their authored layout.
The game supports 4:3 and 16:9 with black bars; Auto uses the Deck's 16:10 view.
Resolution choices follow the active display. The Deck build limits render
scale to 100% to avoid supersampling above the chosen output resolution.
The Deck build stays fullscreen. Resolution choices resize the rendered scene
while the compositor keeps the display mode. Old windowed/borderless settings
automatically use fullscreen. Visual styles, FXAA and Enhanced effects run on
the GPU. The Display page retains Resolution, VSync and Aspect Ratio controls.
For more performance, select 75% render scale in Settings > Graphics.
The Deck's 60/90 Hz display refresh does not change the game's authored timing.

CONTROLS AND SPEECH
Audio uses SteamOS's session sound server through the PulseAudio-compatible
API provided by PipeWire. The launcher prefers pulseaudio instead of raw ALSA
hardware devices. The bundled PulseAudio client locates its private library
beside itself; no Debian-specific library directories are needed on the Deck.

Use Steam Input's Gamepad template to expose a standard SDL controller.
Settings > Controls shows the current bindings and permits remapping.
To exit, select QUIT GAME below BONUS on the main menu, then choose YES in
the YES / NO confirmation. NO is selected initially; B cancels. The title menu uses
one navigation path for all five entries: use the D-pad or left stick and A.
QUIT GAME appears with the other entries during the title reveal.
The original recognizer runs natively. Settings > Microphone selects the Deck
microphone or a headset and shows the live input level. SteamOS must have the
desired microphone selected and unmuted. Hardware microphone/controller tests
still require the actual Deck.

SAVES AND SETTINGS
Linux user data is stored in:
    ~/.local/share/Lifeline/
or $XDG_DATA_HOME/Lifeline when XDG_DATA_HOME is set. The Windows installation
and its saves are separate. To transfer saves, close both copies and copy the
desired Savegame files from the Windows user folder into the Linux user folder.
Keep Linux config.ini and microphone/device settings for the Deck.
Set LIFELINE_USER_DATA before launching to choose another user directory.

DIAGNOSTICS
The launcher always saves startup output to:
    ~/.local/share/Lifeline/launch.log
or to the selected XDG_DATA_HOME/Lifeline or LIFELINE_USER_DATA folder. Startup
failures print the error and show a desktop dialog when a dialog tool exists.
For a CPU scene-rendering fallback:
    sh ./launch.sh --renderer software
To check the copied assets:
    sh ./launch.sh --verify-data
If "sh: cannot open ./launch.sh: No such file or directory" appears, the
terminal is not in the extracted game folder: run pwd and ls -l to check.
If the executable reports "error while loading shared libraries", use the
launcher and keep the complete lib folder beside lifeline. The revised binary
also searches that folder for indirect dependencies when launched directly.
If the launcher reports that execution is not allowed, move the game to the
Deck's internal storage or an executable ext4 microSD card.

The package uses native SDL and FFmpeg. Licenses are in licenses/ and dependency
versions are recorded in runtime-manifest.json. The AMD OpenGL driver,
glibc and C++ runtime come from SteamOS; do not replace them with Windows DLLs.
Game assets remain the property of their owners and are supplied only from
your existing local installation.
