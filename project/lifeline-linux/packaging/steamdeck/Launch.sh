#!/bin/sh
set -u
LIFELINE_PACKAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$LIFELINE_PACKAGE" || exit 1
LIFELINE_LOG=
lifeline_error() {
    printf '%s\n' "$1" >&2
    if [ -n "$LIFELINE_LOG" ] && [ -f "$LIFELINE_LOG" ]; then
        tail -n 35 "$LIFELINE_LOG" >&2
    fi
    # Use the host's libraries for its error dialog. The game's bundled media
    # libraries must not override Qt/GTK dependencies in desktop utilities.
    if [ "${LIFELINE_NO_DIALOG:-0}" != 1 ] && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        if command -v kdialog >/dev/null 2>&1; then
            kdialog --title Lifeline --error "$1

Startup log: ${LIFELINE_LOG:-not available}" || :
        elif command -v zenity >/dev/null 2>&1; then
            zenity --error --title=Lifeline --text="$1

Startup log: ${LIFELINE_LOG:-not available}" || :
        fi
    fi
    exit 1
}
export LIFELINE_USER_DATA="${LIFELINE_USER_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/Lifeline}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export SDL_RENDER_DRIVER=opengl
export LIFELINE_RENDERER=opengl
# SteamOS routes speakers/headsets and microphones through PipeWire's
# PulseAudio-compatible session server. Do not fall through to raw ALSA cards.
export SDL_AUDIO_DRIVER=pulseaudio
mkdir -p "$LIFELINE_USER_DATA" || lifeline_error "Cannot create the Lifeline saves/settings folder: $LIFELINE_USER_DATA"
LIFELINE_LOG="$LIFELINE_USER_DATA/launch.log"
: > "$LIFELINE_LOG" || lifeline_error "Cannot write the startup log: $LIFELINE_LOG"
{
    printf 'Lifeline native Linux startup\nPackage: %s\nUser data: %s\n' "$LIFELINE_PACKAGE" "$LIFELINE_USER_DATA"
    uname -sm
    getconf GNU_LIBC_VERSION 2>/dev/null || :
    printf 'DISPLAY=%s\nWAYLAND_DISPLAY=%s\n' "${DISPLAY:-}" "${WAYLAND_DISPLAY:-}"
    printf 'SDL_AUDIO_DRIVER=%s\n' "$SDL_AUDIO_DRIVER"
} >> "$LIFELINE_LOG" 2>&1
[ -f "$LIFELINE_PACKAGE/lifeline" ] || lifeline_error "The lifeline executable is missing. Copy or extract the entire Steamdeck package."
[ -d "$LIFELINE_PACKAGE/lib" ] || lifeline_error "The lib folder is missing. Copy or extract the entire Steamdeck package."
[ -f "$LIFELINE_PACKAGE/data/manifest.json" ] || lifeline_error "The installed game data is missing. Copy or extract the entire Steamdeck package."
# Folder copies from Windows can lose Linux executable permissions. Running
# this launcher through sh can repair them without touching any game data.
chmod u+x "$LIFELINE_PACKAGE/${0##*/}" >> "$LIFELINE_LOG" 2>&1 || :
if [ ! -x "$LIFELINE_PACKAGE/lifeline" ]; then
    chmod u+x "$LIFELINE_PACKAGE/lifeline" >> "$LIFELINE_LOG" 2>&1 || lifeline_error "Cannot make lifeline executable. Extract the archive into a writable Linux folder on the Deck."
fi
[ -x "$LIFELINE_PACKAGE/lifeline" ] || lifeline_error "This location does not allow execution. Move the Steamdeck folder to the Deck's internal storage or an executable ext4 microSD card."
printf 'Starting game...\n' >> "$LIFELINE_LOG"
# The packaged ELF has a transitive $ORIGIN/lib runtime path. Do not export a
# bundled LD_LIBRARY_PATH: SDL may launch the host's zenity error dialog, whose
# system GLib libraries must not be replaced with the bundled media libraries.
if "$LIFELINE_PACKAGE/lifeline" "$@" >> "$LIFELINE_LOG" 2>&1; then
    printf 'Game exited successfully.\n' >> "$LIFELINE_LOG"
else
    LIFELINE_STATUS=$?
    printf '\nGame exit status: %s\n' "$LIFELINE_STATUS" >> "$LIFELINE_LOG"
    case "$LIFELINE_STATUS" in 130|143) exit "$LIFELINE_STATUS";; esac
    lifeline_error "Lifeline could not start or stopped unexpectedly (exit $LIFELINE_STATUS). Open the startup log for the error: $LIFELINE_LOG"
fi
