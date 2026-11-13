#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace lifeline {
// Native port of the 32-sample transform used by the original microphone HUD.
// These are display measurements, never speech confidence or pronunciation scores.
class MicrophoneSpectrum {
    std::array<std::int16_t,32> input_{};
    std::array<float,32> cosine_{},sine_{};
    std::array<float,16> bands_{};
    unsigned used_=0;
    void transform();
public:
    MicrophoneSpectrum();
    void push(std::span<const std::int16_t>);
    void reset();
    const std::array<float,16>& bands()const{return bands_;}
};

// Original 60 Hz display smoothing, independent of audio device block sizes.
class MicrophoneSpectrumDisplay {
    std::array<float,16> previous_{},decay_{},levels_{};
public:
    void advance(const std::array<float,16>& decibels);
    const std::array<float,16>& levels()const{return levels_;}
};
}
