cmake_minimum_required(VERSION 3.24)
project(LifelineInstaller VERSION 0.1.0 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The standalone Lifeline installer currently requires Windows and MSVC.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(cmake/PrepareDependencies.cmake)
get_filename_component(LIFELINE_WORKSPACE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LIFELINE_OUTPUT "${LIFELINE_WORKSPACE}/build" CACHE PATH "Standalone installer output")
set(LIFELINE_EMBEDDED_GAME_OUTPUT "${LIFELINE_NATIVE_WORK}/game")
set(LIFELINE_ISO "${LIFELINE_WORKSPACE}/sources/iso/Lifeline.iso" CACHE FILEPATH "Original read-only ISO")
set(LIFELINE_SETUP_RECIPE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/recipe.json" CACHE FILEPATH "Optional metadata-only source distribution recipe")
if(EXISTS "${LIFELINE_SETUP_RECIPE}")
    set(LIFELINE_PACKAGE_INPUT "${LIFELINE_SETUP_RECIPE}")
    add_custom_target(lifeline-reference-data DEPENDS "${LIFELINE_SETUP_RECIPE}")
else()
    set(LIFELINE_REFERENCE_DATA "${LIFELINE_NATIVE_WORK}/reference/data")
    set(LIFELINE_MOVIE_CACHE "${LIFELINE_OUTPUT}/data" CACHE PATH "Optional previous movie conversion cache")
    if(NOT EXISTS "${LIFELINE_MOVIE_CACHE}/media/movies/s0.json" AND EXISTS "${LIFELINE_WORKSPACE}/dev/asset-layout-migration/fresh/data/media/movies/s0.json")
        set(LIFELINE_MOVIE_CACHE "${LIFELINE_WORKSPACE}/dev/asset-layout-migration/fresh/data")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(LIFELINE_REFERENCE "${LIFELINE_WORKSPACE}/dev/installer-reference")
    file(GLOB LIFELINE_REFERENCE_SCRIPTS CONFIGURE_DEPENDS "${LIFELINE_REFERENCE}/tools/*.py")
    add_custom_command(OUTPUT "${LIFELINE_REFERENCE_DATA}/manifest.json"
        COMMAND Python3::Interpreter -B "${LIFELINE_REFERENCE}/tools/prepare_movies.py"
            "${LIFELINE_ISO}" "${LIFELINE_REFERENCE_DATA}/media/movies"
            --legacy-output "${LIFELINE_MOVIE_CACHE}/media/movies"
            --python-libs "${LIFELINE_WORKSPACE}/dev/python" --work "${LIFELINE_NATIVE_WORK}/reference-conversion"
        COMMAND Python3::Interpreter -B "${LIFELINE_REFERENCE}/tools/prepare_assets.py"
            "${LIFELINE_ISO}" "${LIFELINE_REFERENCE}/assets.json" "${LIFELINE_REFERENCE_DATA}"
        DEPENDS "${LIFELINE_ISO}" ${LIFELINE_REFERENCE_SCRIPTS}
            "${LIFELINE_REFERENCE}/assets.json" "${LIFELINE_REFERENCE}/layout.json" "${LIFELINE_REFERENCE}/effects.json"
        COMMENT "Prepare development reference metadata for native converter verification" VERBATIM)
    add_custom_target(lifeline-reference-data DEPENDS "${LIFELINE_REFERENCE_DATA}/manifest.json")
    set(LIFELINE_PACKAGE_INPUT "${LIFELINE_REFERENCE_DATA}")
endif()

# The separate installer build updates and embeds the native game executable.
# Keep this cache separate from the developer's game build and its output settings.
set(LIFELINE_GAME_BUILD "${LIFELINE_NATIVE_WORK}/game-cmake")
add_custom_target(lifeline-game
    COMMAND "${CMAKE_COMMAND}" -S "${LIFELINE_WORKSPACE}/project/lifeline" -B "${LIFELINE_GAME_BUILD}"
        -G Ninja -DCMAKE_BUILD_TYPE=Release "-DLIFELINE_OUTPUT=${LIFELINE_EMBEDDED_GAME_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" --build "${LIFELINE_GAME_BUILD}" --target lifeline
    BYPRODUCTS "${LIFELINE_EMBEDDED_GAME_OUTPUT}/lifeline.exe" VERBATIM)

add_library(lifeline-setup-common STATIC src/common.cpp src/disc.cpp src/converters.cpp src/package.cpp)
target_include_directories(lifeline-setup-common PUBLIC include "${LIFELINE_NATIVE_DEPS}")
target_compile_definitions(lifeline-setup-common PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(lifeline-setup-common PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(lifeline-setup-common PUBLIC bcrypt cabinet shell32 ole32)
add_library(lifeline-setup-movies STATIC src/movie_converter.cpp)
add_dependencies(lifeline-setup-movies lifeline-movie-dependencies)
target_link_libraries(lifeline-setup-movies PRIVATE lifeline-setup-common)
target_include_directories(lifeline-setup-movies SYSTEM PRIVATE "${LIFELINE_FFMPEG}/include")
foreach(library avcodec avformat avutil swscale)
    target_link_libraries(lifeline-setup-movies PRIVATE "${LIFELINE_FFMPEG}/lib/${library}.lib")
endforeach()
target_link_libraries(lifeline-setup-movies PRIVATE mfuuid strmiids user32)
target_compile_options(lifeline-setup-movies PRIVATE /W4 /permissive- /utf-8)
add_executable(lifeline-package src/make_package.cpp)
target_link_libraries(lifeline-package PRIVATE lifeline-setup-common)
set_target_properties(lifeline-package PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LIFELINE_NATIVE_WORK}/packaging")
set(LIFELINE_PACKAGE "${LIFELINE_NATIVE_WORK}/packaging/setup.pak")
add_custom_target(lifeline-setup-payload
    COMMAND "$<TARGET_FILE:lifeline-package>" "${LIFELINE_PACKAGE_INPUT}" "${LIFELINE_EMBEDDED_GAME_OUTPUT}/lifeline.exe"
        "${LIFELINE_FFMPEG_SOURCE}/COPYING.LGPLv2.1"
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/third-party-notices.txt" "${LIFELINE_PACKAGE}"
    DEPENDS lifeline-package lifeline-game lifeline-reference-data lifeline-movie-dependencies
    BYPRODUCTS "${LIFELINE_PACKAGE}" VERBATIM)
set(LIFELINE_SETUP_ICON "${LIFELINE_GAME_BUILD}/generated/lifeline.ico")
if(EXISTS "${LIFELINE_WORKSPACE}/project/lifeline/packaging/source.ico")
    set(LIFELINE_SETUP_ICON "${LIFELINE_WORKSPACE}/project/lifeline/packaging/source.ico")
endif()
set(LIFELINE_SETUP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/setup.manifest")
configure_file(src/installer.rc.in "${CMAKE_CURRENT_BINARY_DIR}/installer.rc" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/installer.rc" PROPERTIES OBJECT_DEPENDS "${LIFELINE_PACKAGE};${LIFELINE_SETUP_ICON}")
add_executable(LifelineSetup WIN32 src/main.cpp src/install.cpp src/ui.cpp "${CMAKE_CURRENT_BINARY_DIR}/installer.rc")
add_dependencies(LifelineSetup lifeline-setup-payload)
target_link_libraries(LifelineSetup PRIVATE lifeline-setup-common lifeline-setup-movies comctl32 comdlg32 dwmapi)
target_compile_options(LifelineSetup PRIVATE /W4 /permissive- /utf-8)
target_link_options(LifelineSetup PRIVATE /MANIFEST:NO)
set_target_properties(LifelineSetup PROPERTIES OUTPUT_NAME Installer RUNTIME_OUTPUT_DIRECTORY "${LIFELINE_OUTPUT}")
