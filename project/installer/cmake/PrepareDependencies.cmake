set(LIFELINE_NATIVE_WORK "${CMAKE_CURRENT_SOURCE_DIR}/../../dev/installer-native")
get_filename_component(LIFELINE_NATIVE_WORK "${LIFELINE_NATIVE_WORK}" ABSOLUTE)
set(LIFELINE_NATIVE_DEPS "${LIFELINE_NATIVE_WORK}/dependencies")
file(MAKE_DIRECTORY "${LIFELINE_NATIVE_DEPS}")
function(lifeline_download name url hash)
    set(path "${LIFELINE_NATIVE_DEPS}/${name}")
    get_filename_component(parent "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    if(EXISTS "${path}")
        file(SHA256 "${path}" actual)
        if(actual STREQUAL hash)
            return()
        endif()
    endif()
    file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON SHOW_PROGRESS)
endfunction()
lifeline_download(json.hpp
    "https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp"
    "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63")
set(LIFELINE_MOVIE_DEPS "${LIFELINE_NATIVE_DEPS}/minimal-tools")
lifeline_download(minimal-tools/ffmpeg-source.tar.xz
    "https://ffmpeg.org/releases/ffmpeg-8.1.3.tar.xz"
    "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3")
lifeline_download(minimal-tools/make.pkg.tar.zst
    "https://mirror.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst"
    "af0bdba17f06fe037f0194069adaa31a8fe45f1a11381501896aea1fae37bd5d")
lifeline_download(minimal-tools/nasm.zip
    "https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip"
    "3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6")
set(LIFELINE_FFMPEG_SOURCE "${LIFELINE_MOVIE_DEPS}/ffmpeg-8.1.3")
set(LIFELINE_FFMPEG "${LIFELINE_NATIVE_DEPS}/ffmpeg-minimal")
if(NOT EXISTS "${LIFELINE_FFMPEG_SOURCE}/configure")
    file(ARCHIVE_EXTRACT INPUT "${LIFELINE_MOVIE_DEPS}/ffmpeg-source.tar.xz" DESTINATION "${LIFELINE_MOVIE_DEPS}")
endif()
if(NOT EXISTS "${LIFELINE_MOVIE_DEPS}/usr/bin/make.exe")
    file(ARCHIVE_EXTRACT INPUT "${LIFELINE_MOVIE_DEPS}/make.pkg.tar.zst" DESTINATION "${LIFELINE_MOVIE_DEPS}")
endif()
if(NOT EXISTS "${LIFELINE_MOVIE_DEPS}/nasm-2.16.03/nasm.exe")
    file(ARCHIVE_EXTRACT INPUT "${LIFELINE_MOVIE_DEPS}/nasm.zip" DESTINATION "${LIFELINE_MOVIE_DEPS}")
endif()
find_package(Git REQUIRED)
get_filename_component(git_bin "${GIT_EXECUTABLE}" DIRECTORY)
get_filename_component(git_root "${git_bin}" DIRECTORY)
find_program(LIFELINE_BASH NAMES bash.exe HINTS "${git_root}/usr/bin" REQUIRED NO_DEFAULT_PATH)
set(LIFELINE_FFMPEG_LIBRARIES)
foreach(library avcodec avformat avutil swscale)
    list(APPEND LIFELINE_FFMPEG_LIBRARIES "${LIFELINE_FFMPEG}/lib/${library}.lib")
endforeach()
add_custom_command(OUTPUT ${LIFELINE_FFMPEG_LIBRARIES}
    COMMAND "${LIFELINE_BASH}" --noprofile --norc "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildMovieDependencies.sh"
        "${LIFELINE_NATIVE_WORK}" "${LIFELINE_MOVIE_DEPS}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildMovieDependencies.sh" "${LIFELINE_FFMPEG_SOURCE}/configure"
    COMMENT "Build the minimal static movie codecs (no FFmpeg executables or DLLs)" VERBATIM)
add_custom_target(lifeline-movie-dependencies DEPENDS ${LIFELINE_FFMPEG_LIBRARIES})
