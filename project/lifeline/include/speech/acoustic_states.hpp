#pragma once
#include "speech/acoustic_model.hpp"

namespace lifeline {
struct SpeechAcousticFrame {
    std::uint16_t external_cost;
    std::int16_t parameter30;
    std::int16_t parameter31;
};
// Builds model-ID costs from packed raw-row costs. The first IDs are composite
// states, not raw rows; context/session parameters must be supplied explicitly.
class SpeechAcousticStates {
    std::size_t raw_count_;
    std::uint16_t external_row_;
    std::vector<std::vector<std::uint16_t>> groups_;
    std::vector<std::vector<SpeechCompositeTerm>> composites_;
public:
    explicit SpeechAcousticStates(const OriginalSpeechData& data);
    std::vector<std::int16_t> evaluate(std::span<const std::uint16_t> raw_costs,
                                      SpeechAcousticFrame frame) const;
};
}
