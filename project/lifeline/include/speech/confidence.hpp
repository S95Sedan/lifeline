#pragma once
#include <cstdint>

namespace lifeline {
// Recovered hypothesis calibration at 0x215fe4..0x216074. The input is
// (hypothesis cost - reference cost) / scored frames AFTER boundary removal.
// It is not the margin to the second-ranked dictionary word.
float speech_confidence_curve(float mean_cost_difference);
unsigned speech_hypothesis_score(std::uint32_t hypothesis_cost,
                                 std::uint32_t reference_cost,
                                 std::uint32_t scored_frames);
}
