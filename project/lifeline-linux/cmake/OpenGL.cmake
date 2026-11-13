# Translate the shared native raster shader at build time. No shader compiler
# or SPIR-V/Vulkan runtime is needed by the resulting OpenGL executable.
find_program(LIFELINE_DXC NAMES dxc REQUIRED)
find_program(LIFELINE_SPIRV_CROSS NAMES spirv-cross REQUIRED)
set(_gl_shader "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/shaders/scene_raster.hlsl")
function(lifeline_gl_shader name entry)
set(_gl_defines -D LIFELINE_OPENGL=1)
if(NOT name STREQUAL "plain")
    list(APPEND _gl_defines -D ENHANCED_RASTER=1)
endif()
set(_gl_spirv "${LIFELINE_GENERATED}/rendering/opengl_${name}.spv")
set(_gl_glsl "${LIFELINE_GENERATED}/rendering/opengl_${name}.comp")
add_custom_command(OUTPUT "${_gl_spirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIFELINE_GENERATED}/rendering"
    COMMAND "${LIFELINE_DXC}" -spirv -T cs_6_0 -E ${entry} ${_gl_defines}
        -fspv-target-env=vulkan1.0
        -fvk-bind-register t0 0 0 0 -fvk-bind-register t1 0 1 0
        -fvk-bind-register t2 0 2 0 -fvk-bind-register t3 0 3 0
        -fvk-bind-register t4 0 5 0 -fvk-bind-register t5 0 6 0 -fvk-bind-register t6 0 7 0
        -fvk-bind-register u0 0 4 0 -fvk-bind-register u1 0 5 0
        -fvk-bind-register u2 0 6 0 -fvk-bind-register u3 0 7 0
        -fvk-bind-register b0 0 8 0 -Fo "${_gl_spirv}" "${_gl_shader}"
    DEPENDS "${_gl_shader}" VERBATIM)
add_custom_command(OUTPUT "${_gl_glsl}"
    COMMAND "${LIFELINE_SPIRV_CROSS}" "${_gl_spirv}" --version 430 --no-es --output "${_gl_glsl}"
    DEPENDS "${_gl_spirv}" VERBATIM)
add_custom_command(OUTPUT "${LIFELINE_GENERATED}/rendering/opengl_${name}_shader.hpp"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${_gl_glsl}
        -DOUTPUT=${LIFELINE_GENERATED}/rendering/opengl_${name}_shader.hpp
        -DSYMBOL=opengl_${name}_shader
        -P "${CMAKE_CURRENT_LIST_DIR}/EmbedGlsl.cmake"
    DEPENDS "${_gl_glsl}" "${CMAKE_CURRENT_LIST_DIR}/EmbedGlsl.cmake" VERBATIM)
target_sources(lifeline-flash PRIVATE "${LIFELINE_GENERATED}/rendering/opengl_${name}_shader.hpp")
endfunction()
lifeline_gl_shader(scene main)
lifeline_gl_shader(plain main)
lifeline_gl_shader(aa antiAlias)
lifeline_gl_shader(style visualStyle)
lifeline_gl_shader(ambient ambientLighting)
lifeline_gl_shader(enhanced enhancedScene)
target_sources(lifeline-flash PRIVATE "${LIFELINE_PLATFORM_DIR}/src/rendering/opengl_renderer.cpp")
target_include_directories(lifeline-flash PRIVATE "${LIFELINE_GENERATED}/rendering")
target_compile_definitions(lifeline-flash PUBLIC LIFELINE_HAS_OPENGL=1)
