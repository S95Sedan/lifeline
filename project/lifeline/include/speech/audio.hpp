#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace lifeline {
std::vector<std::int16_t> stretch_speech_audio(std::span<const std::int16_t> audio,float factor);
// PC microphone conditioning, independent of the expected command.
std::vector<std::int16_t> prepare_speech_audio(std::span<const std::int16_t>);
bool speech_covers_capture(std::span<const std::int16_t>,std::size_t first,std::size_t last);
}
