#!/usr/bin/env bash
set -e
git_bin=${BASH%/*}
export PATH="$PATH:$git_bin"
root=$(cygpath -u "$1")
deps=$(cygpath -u "$2")
export PATH="$PATH:$git_bin:$deps/usr/bin:$deps/nasm-2.16.03"
script=$(realpath "$0")
mkdir -p "$root/ffmpeg-build"
cd "$root/ffmpeg-build"
signature=$(sha256sum "$script" "$deps/ffmpeg-8.1.3/configure")
if [ ! -f config.h ] || [ "$signature" != "$(cat configure.signature 2>/dev/null)" ]; then
    "$deps/ffmpeg-8.1.3/configure" \
        --toolchain=msvc --arch=x86_64 --target-os=win32 \
        --prefix="$root/dependencies/ffmpeg-minimal" \
        --enable-static --disable-shared --disable-programs --disable-doc \
        --disable-debug --enable-small \
        --disable-everything --disable-autodetect --disable-network \
        --disable-avdevice --disable-avfilter --disable-swresample \
        --enable-decoder=ipu,mpeg2video,h264,adpcm_psx \
        --enable-encoder=h264_mf,aac --enable-mediafoundation --enable-d3d11va \
        --enable-muxer=mp4 --enable-demuxer=mov --enable-protocol=file \
        --enable-parser=h264 --enable-swscale --extra-cflags=-MT \
        --enable-bsf=av1_metadata \
        > "$root/ffmpeg-configure.log" 2>&1
    # The MP4 muxer needs AV1 coded-bitstream support even when output is H.264.
    # Enabling that component also avoids an empty dispatcher table in MSVC.
    printf '%s' "$signature" > configure.signature
fi
make -r -j8 > "$root/ffmpeg-build.log" 2>&1
make -r install >> "$root/ffmpeg-build.log" 2>&1
