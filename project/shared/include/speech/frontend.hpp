#pragma once
#include "speech/acoustic_model.hpp"
#include "speech/normalization.hpp"
#include <memory>

namespace lifeline {
struct SpeechFeatureFrame {
    std::vector<float> features;
    bool active;
};
// Streaming native processing graph. Input blocks are 110 signed samples at
// 11000 Hz. Session activation remains explicit rather than guessed from text.
class SpeechFrontend {
    struct State;
    std::unique_ptr<State> state_;
public:
    SpeechFrontend(const OriginalSpeechData& language,SpeechNormalizationData normalization,
                   std::uint32_t active_warps,bool relative_energy);
    ~SpeechFrontend();
    std::vector<SpeechFeatureFrame> push_block(std::span<const std::int16_t> pcm);
    std::vector<SpeechFeatureFrame> finish();
    unsigned selected_branch()const;
};
}
