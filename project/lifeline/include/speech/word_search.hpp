#pragma once
#include "speech/acoustic_model.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lifeline {
struct SpeechWordPath {
    std::uint32_t context_id;
    std::vector<std::uint16_t> states;
};
struct SpeechWordCost {
    std::uint32_t context_id;
    std::int64_t cost;
};

// Exact, unpruned alignment of a bounded utterance against explicit word paths.
// The stay/advance recurrence is recovered from 0x2692b8..0x269448.
// Endpoint selection, grammar activation and confidence are separate stages:
// a lowest-cost path is NOT, by itself, an accepted recognition result.
class SpeechWordSearch {
    struct Path {
        SpeechWordPath definition;
        std::vector<std::int64_t> costs;
    };
    std::vector<Path> paths_;
    std::size_t frames_ = 0;
    std::size_t required_states_ = 0;
public:
    explicit SpeechWordSearch(std::vector<SpeechWordPath> paths);
    // Expand context model references in file order; no invented silence states.
    static std::vector<SpeechWordPath> command_paths(const OriginalSpeechData& data);
    void reset();
    void push(std::span<const std::int16_t> state_costs);
    // Only paths which consumed at least one frame in EVERY state are returned.
    // Stable order for equal costs preserves context order. No confidence score.
    std::vector<SpeechWordCost> ranked() const;
    std::size_t frames() const { return frames_; }
    std::size_t required_states() const { return required_states_; }
};
}
