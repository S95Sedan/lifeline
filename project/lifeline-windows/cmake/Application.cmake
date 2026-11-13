set(LIFELINE_APP_ICON "" CACHE FILEPATH "Optional source-owned Windows icon, avoiding a build-time disc dependency")
if(NOT LIFELINE_APP_ICON AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/source.ico")
    set(LIFELINE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/source.ico")
endif()
    enable_language(RC)
    if(LIFELINE_APP_ICON)
        get_filename_component(LIFELINE_GENERATED_APP_ICON "${LIFELINE_APP_ICON}" ABSOLUTE)
        if(NOT EXISTS "${LIFELINE_GENERATED_APP_ICON}")
            message(FATAL_ERROR "LIFELINE_APP_ICON does not exist: ${LIFELINE_APP_ICON}")
        endif()
    else()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(LIFELINE_GENERATED_APP_ICON "${LIFELINE_GENERATED}/lifeline.ico")
        add_custom_command(OUTPUT "${LIFELINE_GENERATED_APP_ICON}"
            COMMAND Python3::Interpreter -B "${LIFELINE_INSTALLER}/tools/prepare_app_icon.py"
                "${LIFELINE_ISO}" "${LIFELINE_GENERATED_APP_ICON}"
            DEPENDS "${LIFELINE_ISO}" "${LIFELINE_INSTALLER}/tools/prepare_app_icon.py"
            VERBATIM)
    endif()
    configure_file("${LIFELINE_PLATFORM_DIR}/src/app/lifeline.rc.in" "${LIFELINE_GENERATED}/lifeline.rc" @ONLY)
    set_source_files_properties("${LIFELINE_GENERATED}/lifeline.rc" PROPERTIES OBJECT_DEPENDS "${LIFELINE_GENERATED_APP_ICON}")
    target_sources(lifeline PRIVATE "${LIFELINE_GENERATED}/lifeline.rc" "${LIFELINE_GENERATED_APP_ICON}")
if(MSVC)
    # Start without a console flash; config.cpp creates one only for debug mode.
    target_link_options(lifeline PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
