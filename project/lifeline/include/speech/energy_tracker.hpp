#pragma once
#include <array>
#include <bit>

namespace lifeline {
struct SpeechEnergySettings {
    unsigned warmup=2, onset=10, hangover=40, level_period=50;
    float initial_signal_db=15, activity_offset_db=std::bit_cast<float>(0x410b0a3du);
    float noise_scale_parameter=1000, channel_offset=0;
    bool linear_activity=false, relative_level=false;
};
struct SpeechEnergyFrame {
    float activity, energy, signal_level, onset_threshold, noise_threshold, noise_level, noise_spread;
};
// Numerical energy tracking core shared by modules 06/0b, 0x278670.
// Session notifications and decoder termination are separate from this core.
class SpeechEnergyTracker {
    SpeechEnergySettings settings_;
    std::array<unsigned,150> histogram_{};
    std::array<unsigned,40> recent_bins_{};
    std::array<float,150> bin_energy_{};
    std::array<float,20> delayed_energy_{};
    unsigned count_=0, bin_next_=0, energy_next_=0, warmup_=0;
    unsigned above_=0, hangover_=0, level_frames_=0;
    float signal_=0, noise_=0, spread_=1, peak_=0, threshold_=0, noise_threshold_=0;
    float activity_offset_,initial_signal_,minimum_onset_,histogram_floor_;
    bool initialized_=false;
public:
    explicit SpeechEnergyTracker(SpeechEnergySettings settings);
    void reset();
    void set_relative_level(bool relative);
    SpeechEnergyFrame push(float log_energy);
};
}
