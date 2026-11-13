#include "speech/acoustic_states.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lifeline {
SpeechAcousticStates::SpeechAcousticStates(const OriginalSpeechData& data)
    : raw_count_(data.state_streams().size()), external_row_(data.external_acoustic_row()),
      groups_(data.acoustic_groups()), composites_(data.composite_states()) {}
std::vector<std::int16_t> SpeechAcousticStates::evaluate(
    std::span<const std::uint16_t> raw_costs, SpeechAcousticFrame frame) const {
    if(raw_costs.size()!=raw_count_ || frame.external_cost>=0x7fff)
        throw std::invalid_argument("Invalid speech acoustic frame");
    std::vector<std::int16_t> result(composites_.size());
    for(auto cost:raw_costs){
        if(cost>=0x7fff)throw std::invalid_argument("Invalid speech raw acoustic cost");
        result.push_back(static_cast<std::int16_t>(cost));
    }
    std::vector<int> groups;
    for(const auto& members:groups_){
        int minimum=std::numeric_limits<int>::max();
        for(auto row:members)
            minimum=std::min(minimum,int(row==external_row_?frame.external_cost:raw_costs[row]));
        groups.push_back(minimum);
    }
    for(std::size_t i=0;i<composites_.size();++i){
        int minimum=std::numeric_limits<int>::max();
        for(const auto& term:composites_[i]){
            // Arithmetic right shift, followed by the two independent flags
            // (0x27b79c..0x27b860). Negative constants must retain their sign.
            int bias=int(term.encoded_bias)>>2;
            if(term.encoded_bias&1)bias+=frame.parameter31;
            if(term.encoded_bias&2)bias+=frame.parameter30;
            // The original stores the decoded bias in a signed halfword.
            if(bias < -32768 || bias>32767)
                throw std::invalid_argument("Speech acoustic bias outside recovered range");
            minimum=std::min(minimum,groups[term.group]+bias);
        }
        minimum=std::max(0,minimum); // 0x27cd20..0x27cd30.
        if(minimum>=0x7fff)throw std::invalid_argument("Speech composite cost outside decoder range");
        result[i]=static_cast<std::int16_t>(minimum);
    }
    return result;
}
}
