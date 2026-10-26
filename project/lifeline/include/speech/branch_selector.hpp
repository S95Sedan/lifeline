#pragma once
#include "speech/acoustic_search.hpp"
#include <deque>

namespace lifeline {
class SpeechBranchSelector {
    OriginalSpeechAcoustics acoustics_;
    std::vector<bool> enabled_;
    std::vector<float> scores_;
    std::deque<std::vector<std::vector<float>>> queue_;
    unsigned selected_=0, mode_=0, averages_=0;
    float total_weight_=0, pending_weight_=0;
    bool locked_=false, automatic_lock_,finished_=false;
    std::vector<float> emit();
public:
    SpeechBranchSelector(const OriginalSpeechData& data,std::uint32_t active_mask,bool automatic_lock);
    void reset();
    std::vector<std::vector<float>> push(const std::vector<std::vector<float>>& cepstra,float weight);
    std::vector<std::vector<float>> finish();
    unsigned selected_branch()const { return selected_; }
};
}
