#include "speech/word_decoder.hpp"
#include "speech/confidence.hpp"
#include <limits>
#include <stdexcept>

namespace lifeline {
SpeechWordDecoder::SpeechWordDecoder(const OriginalSpeechData& data)
    : acoustics_(data), states_(data), search_(SpeechWordSearch::command_paths(data)),
      reference_state_(data.hypothesis_reference_state()) {
    if (search_.required_states() > data.state_streams().size()+data.composite_states().size())
        throw std::invalid_argument("Speech context state outside acoustic model");
}
void SpeechWordDecoder::push(std::span<const float> features, SpeechAcousticFrame frame) {
    const auto codes = acoustics_.quantize(features);
    const auto packed_costs = acoustics_.state_costs(codes);
    const auto costs=states_.evaluate(packed_costs,frame);
    const auto cumulative=reference_history_.back()+std::uint16_t(costs[reference_state_]);
    // Reserve before advancing search, so a failed allocation cannot desync it.
    if(reference_history_.size()==reference_history_.capacity())
        reference_history_.reserve(reference_history_.size()*2);
    search_.push(costs);
    reference_history_.push_back(cumulative);
}
std::uint64_t SpeechWordDecoder::reference_cost(std::size_t begin,std::size_t end) const {
    if(begin>=end || end>=reference_history_.size())
        throw std::invalid_argument("Invalid speech scoring interval");
    return reference_history_[end]-reference_history_[begin];
}
unsigned SpeechWordDecoder::score_interval(std::uint32_t hypothesis_cost,
                                          std::size_t begin,std::size_t end) const {
    const auto reference=reference_cost(begin,end);
    if(reference>std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Speech reference history exceeds original score range");
    return speech_hypothesis_score(hypothesis_cost,std::uint32_t(reference),std::uint32_t(end-begin));
}
}
