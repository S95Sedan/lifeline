#pragma once
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace lifeline {
// 0x27ad18: delayed temporal features for the original codebook search.
// Input is energy, C0, C1..CN from OriginalSpeechCepstrum; C0 is discarded.
class OriginalSpeechFeatures {
    unsigned coefficients_,next_=0;
    std::size_t frames_=0;
    std::array<std::vector<float>,8> ring_;
    bool finished_=false;
    std::vector<float> output(unsigned slot,std::size_t phase)const;
public:
    explicit OriginalSpeechFeatures(unsigned coefficients);
    void reset();
    std::optional<std::vector<float>> push(std::span<const float> cepstrum);
    // Drains the original stage's remaining slots without inventing future
    // frames. Repeated finish calls return no data.
    std::vector<std::vector<float>> finish();
};
}
