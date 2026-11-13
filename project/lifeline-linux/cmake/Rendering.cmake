if(NOT LIFELINE_OPENGL)
    message(FATAL_ERROR "Native Linux builds require OpenGL")
endif()
include("${LIFELINE_PLATFORM_DIR}/cmake/OpenGL.cmake")
