#pragma once
#include "speech/spectral_baseline.hpp"
#include <deque>
#include <cstddef>

namespace lifeline {
struct SpeechNormalizationData {
    SpectralProjectionData projection;
    std::vector<float> initial_baseline, band_floor, soft_floor;
};
SpeechNormalizationData read_speech_normalization(std::span<const std::byte> data);
struct SpeechNormalizationSettings {
    unsigned queue_capacity=50, quiet_drain=15;
    float noise_offset, difference_floor, energy_floor;
    float adaptation_limit=50, initial_weight=5;
};
struct SpeechNormalizedFrame {
    float energy;
    std::vector<float> bands;
};
// Original modules 0d/0e/0f. Weight and energy reference are supplied by the
// endpoint stages; neither is inferred from recognition success.
class SpeechNormalization {
    SpeechNormalizationData data_;
    SpeechNormalizationSettings settings_;
    SpeechSpectralBaseline projection_;
    std::vector<float> mean_, baseline_;
    std::deque<SpeechNormalizedFrame> queue_;
    unsigned mode_=2, quiet_=0;
    float weight_=0, shift_weight_=0.5f, dirty_=0, energy_reference_=100000;
    bool valid_=false, finished_=false;
    float soft_floor(float negative_difference) const;
    SpeechNormalizedFrame emit();
public:
    SpeechNormalization(SpeechNormalizationData data, SpeechNormalizationSettings settings);
    void reset();
    std::vector<SpeechNormalizedFrame> push(std::span<const float> bands,
        std::span<const float> noise, float weight, float energy, float energy_reference);
    std::vector<SpeechNormalizedFrame> finish();
    std::size_t pending() const { return queue_.size(); }
};
}
