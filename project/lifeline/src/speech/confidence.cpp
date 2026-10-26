#include "speech/confidence.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline {
namespace {
constexpr float bits(std::uint32_t value) { return std::bit_cast<float>(value); }
}
float speech_confidence_curve(float mean_cost_difference) {
    if (!std::isfinite(mean_cost_difference))
        throw std::invalid_argument("Non-finite speech confidence input");
    if (mean_cost_difference >= -26.0f && mean_cost_difference <= 78.0f)
        return (91.0f - mean_cost_difference) * bits(0x4299cccc);
    const float scaled = mean_cost_difference * bits(0x3daefd7f);
    const float offset = mean_cost_difference < -26.0f ? bits(0x3cdfe537) : bits(0xc08ef28e);
    const float exponential = std::exp(scaled + offset);
    return 10000.0f / (1.0f + exponential);
}
unsigned speech_hypothesis_score(std::uint32_t hypothesis_cost,
                                 std::uint32_t reference_cost,
                                 std::uint32_t scored_frames) {
    if (!scored_frames)
        throw std::invalid_argument("Speech confidence requires a nonempty scored interval");
    // The original converts each unsigned accumulator separately before
    // subtracting. Subtracting unsigned integers first would wrap good matches.
    const float difference = static_cast<float>(hypothesis_cost) - static_cast<float>(reference_cost);
    const float mean = difference / static_cast<float>(scored_frames);
    return static_cast<unsigned>(speech_confidence_curve(mean));
}
}
