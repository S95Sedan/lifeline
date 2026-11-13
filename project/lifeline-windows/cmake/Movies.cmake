target_sources(lifeline-movie-decoder PRIVATE "${LIFELINE_PLATFORM_DIR}/src/platform/movie_decoder.cpp")
target_link_libraries(lifeline-movie-decoder PRIVATE mfplat mfreadwrite mfuuid ole32)
