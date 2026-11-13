#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace lifeline {
// Original PCM differencing/window stage. Caller supplies recovered graph
// settings; constructor does not guess the active device clock or hop size.
class OriginalSpeechWindow {
    unsigned hop_,length_;
    std::vector<float> ring_,weights_,last_block_;
    std::int16_t previous_=0,first_=0,last_=0;
    bool started_=false,finished_=false;
    std::vector<float> append(std::span<const float> block);
public:
    OriginalSpeechWindow(unsigned hop,unsigned length);
    std::optional<std::vector<float>> push_block(std::span<const std::int16_t> pcm);
    std::optional<std::vector<float>> finish();
    void reset();
};
}
