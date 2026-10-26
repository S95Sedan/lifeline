#pragma once
#include "disc.hpp"

namespace lifeline::setup {
using Converted = std::map<std::string, Bytes>;
struct Pcm {
    Bytes samples;
    unsigned loop = 0;
    bool repeating = false;
};
Pcm decode_adpcm(View data, std::size_t offset, bool stop_at_end = true);
Bytes wave(View pcm, unsigned channels, unsigned rate);
Bytes stream_pcm(View data, unsigned channels);
Converted voice_bank(View directory, View stream = {});
Converted sound_bank(View header, View body);
Converted music_bank(View sequence, View header, View body, int version);
Bytes convert_member(const std::string &conversion, View data, const Json &record);
Bytes normalization(View elf);
Converted font(View data);
} // namespace lifeline::setup
