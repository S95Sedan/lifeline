# The Windows installer can build its native Linux payload from a clean source
# checkout. A supplied verified runtime remains an explicit build-time override.
set(LIFELINE_LINUX_RUNTIME "" CACHE PATH "Optional verified prebuilt Steam Deck runtime")
if(LIFELINE_LINUX_RUNTIME STREQUAL "${LIFELINE_WORKSPACE}/build/Steamdeck" AND NOT EXISTS "${LIFELINE_LINUX_RUNTIME}/lifeline")
    # Migrate caches created by the former mandatory-prebuilt default.
    set(LIFELINE_LINUX_RUNTIME "" CACHE PATH "Optional verified prebuilt Steam Deck runtime" FORCE)
endif()
if(LIFELINE_LINUX_RUNTIME)
    if(NOT EXISTS "${LIFELINE_LINUX_RUNTIME}/lifeline" OR NOT EXISTS "${LIFELINE_LINUX_RUNTIME}/runtime-manifest.json")
        message(FATAL_ERROR "LIFELINE_LINUX_RUNTIME must contain lifeline and runtime-manifest.json; clear this override to build the runtime automatically")
    endif()
    add_custom_target(lifeline-linux-runtime)
else()
    find_package(Python3 3.11 QUIET COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        set(_python_root "${LIFELINE_NATIVE_WORK}/python")
        set(_python_archive "${LIFELINE_NATIVE_WORK}/python-3.12.10-embed-amd64.zip")
        if(NOT EXISTS "${_python_root}/python.exe")
            file(DOWNLOAD "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip" "${_python_archive}"
                EXPECTED_HASH SHA256=4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3 TLS_VERIFY ON SHOW_PROGRESS)
            file(ARCHIVE_EXTRACT INPUT "${_python_archive}" DESTINATION "${_python_root}")
        endif()
        set(Python3_EXECUTABLE "${_python_root}/python.exe")
    endif()
    set(LIFELINE_LINUX_RUNTIME "${LIFELINE_NATIVE_WORK}/linux-runtime")
    add_custom_target(lifeline-linux-runtime
        COMMAND "${Python3_EXECUTABLE}" -B "${LIFELINE_WORKSPACE}/project/lifeline-linux/tools/build_windows_runtime.py"
            --workspace "${LIFELINE_WORKSPACE}" --cmake "${CMAKE_COMMAND}" --ninja "${CMAKE_MAKE_PROGRAM}"
            --output "${LIFELINE_LINUX_RUNTIME}" --jobs 2
        BYPRODUCTS "${LIFELINE_LINUX_RUNTIME}/lifeline" "${LIFELINE_LINUX_RUNTIME}/runtime-manifest.json"
        COMMENT "Build the native Steam Deck runtime and verified library payload" USES_TERMINAL VERBATIM)
endif()
