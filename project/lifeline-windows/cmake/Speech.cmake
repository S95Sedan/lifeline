    target_sources(lifeline-speech PRIVATE "${LIFELINE_PLATFORM_DIR}/src/platform/speech.cpp")
    target_link_libraries(lifeline-speech PRIVATE sapi ole32)
