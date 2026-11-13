#pragma once
#include "speech/acoustic_search.hpp"
#include "speech/acoustic_states.hpp"
#include "speech/word_search.hpp"

namespace lifeline {
// Connects recovered 38-value features to the disc codebooks and word paths.
// PCM preprocessing and original confidence are not supplied by this class.
class SpeechWordDecoder {
    OriginalSpeechAcoustics acoustics_;
    SpeechAcousticStates states_;
    SpeechWordSearch search_;
    std::uint16_t reference_state_;
    std::vector<std::uint64_t> reference_history_{0};
public:
    explicit SpeechWordDecoder(const OriginalSpeechData& data);
    void push(std::span<const float> features, SpeechAcousticFrame frame);
    void reset() { search_.reset(); reference_history_.resize(1); }
    std::size_t frames() const { return search_.frames(); }
    std::vector<SpeechWordCost> ranked() const { return search_.ranked(); }
    // The caller must supply the original scored interval and its hypothesis
    // cost after boundary removal. Ranked whole-utterance costs are not assumed
    // to be that interval. Invalid/empty intervals never produce a passing score.
    std::uint64_t reference_cost(std::size_t begin, std::size_t end) const;
    unsigned score_interval(std::uint32_t hypothesis_cost,
                            std::size_t begin, std::size_t end) const;
};
}
