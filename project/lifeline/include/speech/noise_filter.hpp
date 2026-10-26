#pragma once
#include <span>
#include <vector>

namespace lifeline {
struct NoiseFilterSettings {
    unsigned bands,noise_delay,energy_start;
    float smoothing,noise_scale,minimum_gain,averaging_limit;
    std::vector<unsigned> region_ends;
};
struct NoiseFilterFrame {
    std::vector<float> log_bands,log_noise;
    float log_energy,log_activity;
};
// Recovered 0x2762d8, used by graph modules 08/09/0a. The activity flag
// comes from the separate endpoint detector; this stage does not invent it.
class SpeechNoiseFilter {
    NoiseFilterSettings settings_;
    unsigned next_=0,quiet_=0,averages_=0;
    float activity_=1;
    std::vector<std::vector<float>> history_;
    std::vector<float> noise_,log_noise_,prior_,ratio_,gain_;
public:
    explicit SpeechNoiseFilter(NoiseFilterSettings settings);
    void reset();
    NoiseFilterFrame push(std::span<const float> bands,bool active);
};
}
