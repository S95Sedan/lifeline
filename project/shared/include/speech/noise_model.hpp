#pragma once
#include "speech/acoustic_model.hpp"

namespace lifeline {
// Module 1f's adaptive external acoustic state, 0x27b3c8 / 0x27d908.
class SpeechNoiseModel {
    std::vector<std::vector<std::uint8_t>> initial_, costs_, counts_;
    std::vector<float> probabilities_;
    std::vector<std::uint8_t> updates_;
    unsigned interval_, quiet_=0, updates_since_normalization_=0;
public:
    explicit SpeechNoiseModel(const OriginalSpeechData& data,unsigned interval=32);
    void reset();
    std::uint16_t push(std::span<const std::uint8_t> codes,bool active);
};
}
