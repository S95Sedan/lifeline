add_library(lifeline-script STATIC
    src/script/battle_targeting.cpp
    src/script/curves.cpp
    src/script/memory.cpp
    src/script/process.cpp
    src/script/program.cpp
    src/script/save_menu.cpp
    src/script/runtime.cpp
    src/script/speech_input.cpp
    src/script/vectors.cpp)
target_include_directories(lifeline-script PUBLIC include)
target_compile_features(lifeline-script PUBLIC cxx_std_20)
add_library(lifeline-flash STATIC
    src/assets/asset_directory.cpp
    src/assets/flash.cpp
    src/assets/flash_actions.cpp
    src/assets/resources.cpp
    src/assets/resource_cache.cpp
    src/ui/combat_panel.cpp
    src/ui/combat_targets.cpp
    src/ui/bonus.cpp
    src/ui/menus.cpp
    src/ui/pc_options_panel.cpp
    src/ui/pc_options.cpp
    src/ui/pc_options_rows.cpp
    src/ui/settings_typography.cpp
    src/ui/input_prompts.cpp
    src/ui/movie_skip_display.cpp
    src/ui/title.cpp
    src/rendering/lighting.cpp
    src/rendering/model.cpp
    src/rendering/presentation.cpp
    src/rendering/raster_scene.cpp
    src/rendering/renderer.cpp
    src/rendering/quality.cpp
    src/rendering/enhanced.cpp
    src/rendering/shadows.cpp
    src/rendering/software/renderer.cpp
    src/ui/world_panel.cpp)
target_include_directories(lifeline-flash PUBLIC include)
if(WIN32)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/hardware/d3d12_raster.hlsl")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/hardware/d3d12_raster.hlsl" LIFELINE_SCENE_SHADER)
    string(LENGTH "${LIFELINE_SCENE_SHADER}" LIFELINE_SHADER_LENGTH)
    set(LIFELINE_SHADER_OFFSET 0)
    set(LIFELINE_SHADER_LITERALS "")
    while(LIFELINE_SHADER_OFFSET LESS LIFELINE_SHADER_LENGTH)
        string(SUBSTRING "${LIFELINE_SCENE_SHADER}" ${LIFELINE_SHADER_OFFSET} 8192 LIFELINE_SHADER_CHUNK)
        string(APPEND LIFELINE_SHADER_LITERALS "R\"LIFELINE_HLSL(${LIFELINE_SHADER_CHUNK})LIFELINE_HLSL\"\n")
        math(EXPR LIFELINE_SHADER_OFFSET "${LIFELINE_SHADER_OFFSET} + 8192")
    endwhile()
    configure_file(src/rendering/hardware/d3d12_shader.hpp.in
        "${LIFELINE_GENERATED}/rendering/d3d12_shader.hpp" @ONLY)
    target_sources(lifeline-flash PRIVATE src/rendering/hardware/d3d12_renderer.cpp)
    target_include_directories(lifeline-flash PRIVATE "${LIFELINE_GENERATED}/rendering")
    target_link_libraries(lifeline-flash PRIVATE d3d12 dxgi d3dcompiler)
endif()
add_library(lifeline-script-assets STATIC
    src/game/session.cpp
    src/game/session_update.cpp
    src/ui/control_panel.cpp
    src/ui/interaction.cpp
    src/ui/inventory_panel.cpp
    src/ui/navigator.cpp
    src/ui/pause.cpp
    src/media/pcm_stream.cpp
    src/media/sequence_playback.cpp
    src/game/collision_grid.cpp
    src/game/routes.cpp
    src/game/scene.cpp
    src/ui/font.cpp
    src/ui/script_display.cpp
    src/script/audio_streams.cpp
    src/script/sound.cpp
    src/script/voice_bank.cpp
    src/game/effects/body_effect.cpp
    src/game/effects/bubble_effect.cpp
    src/game/effects/effect_attachments.cpp
    src/game/effects/effect_geometry.cpp
    src/game/effects/effect_particles.cpp
    src/game/effects/effect_resources.cpp
    src/game/effects/effects.cpp
    src/game/effects/encounter_effect.cpp
    src/game/effects/enemy_smoke.cpp
    src/game/effects/energy_projectile.cpp
    src/game/effects/energy_trail.cpp
    src/game/effects/explosion_effect.cpp
    src/game/effects/globe_display.cpp
    src/game/effects/heron_effect.cpp
    src/game/effects/impact_effect.cpp
    src/game/effects/lightning_effect.cpp
    src/game/effects/plant_effect.cpp
    src/game/effects/poison_effect.cpp
    src/game/effects/screen_effects.cpp
    src/game/effects/shell_effect.cpp
    src/game/effects/smoke_emitter.cpp
    src/game/effects/star_burst.cpp
    src/game/effects/title_environment.cpp
    src/game/effects/wave_effect.cpp
    src/script/drawing.cpp
    src/script/movies.cpp
    src/game/action_table.cpp
    src/game/model_animation.cpp
    src/game/motion_sounds.cpp
    src/game/object_animation.cpp
    src/game/object_attachments.cpp
    src/game/object_motion.cpp
    src/game/objects.cpp
    src/game/skeletal_attachments.cpp
    src/script/resources.cpp
    src/script/speech_context.cpp
    src/script/inventory.cpp
    src/script/storage.cpp
    src/script/analysis_assets.cpp
    src/script/combat_targets.cpp
    src/script/command_messages.cpp
    src/script/command_modes.cpp
    src/script/interface.cpp
    src/script/text.cpp
    src/script/camera.cpp
    src/script/collision.cpp
    src/script/room_visibility.cpp)
target_include_directories(lifeline-script-assets PUBLIC include)
target_compile_features(lifeline-script-assets PUBLIC cxx_std_20)
target_link_libraries(lifeline-script-assets PUBLIC lifeline-script lifeline-flash)
add_library(lifeline-input STATIC
    src/platform/audio_devices.cpp src/platform/microphone_monitor.cpp src/platform/menu_input.cpp src/platform/script_pad.cpp src/platform/bindings.cpp)
target_include_directories(lifeline-input PUBLIC include)
target_compile_features(lifeline-input PUBLIC cxx_std_20)
target_link_libraries(lifeline-input PUBLIC SDL3::SDL3)
target_link_libraries(lifeline-flash PUBLIC lifeline-input)
add_executable(lifeline
    src/ui/pause_display.cpp
    src/ui/presentation_display.cpp
    src/app/main.cpp
    src/app/config.cpp
    src/app/frontend.cpp
    src/ui/cinematic.cpp
    src/app/session_view.cpp
    src/app/settings_overlay.cpp
    src/platform/microphone.cpp
    src/app/speech_diagnostic.cpp)
target_link_libraries(lifeline PRIVATE lifeline-flash lifeline-input SDL3::SDL3)
if(WIN32)
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
    configure_file(src/app/lifeline.rc.in "${LIFELINE_GENERATED}/lifeline.rc" @ONLY)
    set_source_files_properties("${LIFELINE_GENERATED}/lifeline.rc" PROPERTIES OBJECT_DEPENDS "${LIFELINE_GENERATED_APP_ICON}")
    target_sources(lifeline PRIVATE "${LIFELINE_GENERATED}/lifeline.rc" "${LIFELINE_GENERATED_APP_ICON}")
endif()
if(MSVC)
    # Start without a console flash; config.cpp creates one only for debug mode.
    target_link_options(lifeline PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
add_library(lifeline-movie-decoder STATIC)
target_include_directories(lifeline-movie-decoder PUBLIC include)
target_compile_features(lifeline-movie-decoder PUBLIC cxx_std_20)
if(WIN32)
    target_sources(lifeline-movie-decoder PRIVATE src/platform/movie_decoder_windows.cpp)
    target_link_libraries(lifeline-movie-decoder PRIVATE mfplat mfreadwrite mfuuid ole32)
else()
    target_sources(lifeline-movie-decoder PRIVATE src/platform/movie_decoder_unavailable.cpp)
endif()
target_link_libraries(lifeline PRIVATE lifeline-movie-decoder)
add_library(lifeline-movie-playback STATIC src/media/movie_playback.cpp)
target_include_directories(lifeline-movie-playback PUBLIC include)
target_compile_features(lifeline-movie-playback PUBLIC cxx_std_20)
target_link_libraries(lifeline-movie-playback PUBLIC lifeline-flash lifeline-movie-decoder SDL3::SDL3)
target_link_libraries(lifeline-script-assets PUBLIC lifeline-movie-playback)
target_link_libraries(lifeline PRIVATE lifeline-movie-playback)

foreach(target lifeline-input lifeline lifeline-flash)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE include)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
set(LIFELINE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin" CACHE PATH "Native executable output directory")
set_target_properties(lifeline PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${LIFELINE_OUTPUT}")

add_library(lifeline-original-speech STATIC
    src/speech/utterance_search.cpp
    src/speech/frontend.cpp
    src/speech/noise_model.cpp
    src/speech/branch_selector.cpp
    src/speech/normalization_data.cpp
    src/speech/energy_tracker.cpp
    src/speech/activity.cpp
    src/speech/normalization.cpp
    src/speech/spectral_baseline.cpp
    src/speech/acoustic_states.cpp
    src/speech/confidence.cpp
    src/speech/word_decoder.cpp
    src/speech/word_search.cpp
    src/speech/spectrum.cpp
    src/speech/filterbank.cpp
    src/speech/noise_filter.cpp
    src/speech/acoustic_model.cpp
    src/speech/acoustic_search.cpp
    src/speech/pcm_window.cpp
    src/speech/cepstral_transform.cpp
    src/speech/temporal_features.cpp)
target_include_directories(lifeline-original-speech PUBLIC include)
target_compile_features(lifeline-original-speech PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(lifeline-original-speech PRIVATE /W4 /fp:strict)
else()
    target_compile_options(lifeline-original-speech PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_library(lifeline-animation STATIC
    src/game/camera_track.cpp
    src/game/animation.cpp
    src/game/transform_motion.cpp
    src/game/footsteps.cpp
    src/game/gaze.cpp
    src/game/skinning.cpp
    src/game/shadow_geometry.cpp)
target_include_directories(lifeline-animation PUBLIC include)
target_link_libraries(lifeline-animation PUBLIC lifeline-script)
target_compile_features(lifeline-animation PUBLIC cxx_std_20)
set_target_properties(lifeline-animation PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(lifeline-animation PRIVATE /W4 /permissive-)
else()
    target_compile_options(lifeline-animation PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(lifeline PRIVATE lifeline-animation)
target_link_libraries(lifeline-script-assets PUBLIC lifeline-animation)

add_library(lifeline-microphone-spectrum STATIC
    src/speech/microphone_spectrum.cpp)
target_include_directories(lifeline-microphone-spectrum PUBLIC include)
target_compile_features(lifeline-microphone-spectrum PUBLIC cxx_std_20)
target_link_libraries(lifeline PRIVATE lifeline-microphone-spectrum)

add_library(lifeline-speech STATIC
    src/speech/backend.cpp
    src/speech/original_backend.cpp
    src/speech/context_adapter.cpp
    src/speech/recognizer.cpp
    src/speech/task.cpp
    src/speech/command_grammar.cpp
    src/speech/vocabulary.cpp
    src/speech/audio.cpp
    src/speech/recording.cpp)
target_include_directories(lifeline-speech PUBLIC include)
target_compile_features(lifeline-speech PUBLIC cxx_std_20)
target_link_libraries(lifeline-speech PRIVATE lifeline-original-speech)
if(WIN32)
    target_sources(lifeline-speech PRIVATE src/platform/speech_windows.cpp)
    target_link_libraries(lifeline-speech PRIVATE sapi ole32)
endif()
target_link_libraries(lifeline PRIVATE lifeline-speech)

add_library(lifeline-media STATIC
    src/media/audio_preview.cpp
    src/media/music.cpp
    src/media/sound_bank.cpp
    src/media/room_reverb.cpp
    src/ui/hud.cpp)
target_include_directories(lifeline-media PUBLIC include)
target_compile_features(lifeline-media PUBLIC cxx_std_20)
target_link_libraries(lifeline-media PUBLIC lifeline-flash lifeline-microphone-spectrum lifeline-movie-decoder SDL3::SDL3)
target_link_libraries(lifeline PRIVATE lifeline-media)
target_link_libraries(lifeline-script-assets PUBLIC lifeline-media)
target_link_libraries(lifeline-script-assets PUBLIC lifeline-speech)
target_link_libraries(lifeline PRIVATE lifeline-script-assets)
