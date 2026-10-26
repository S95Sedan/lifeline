#include "speech/branch_selector.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lifeline {
SpeechBranchSelector::SpeechBranchSelector(const OriginalSpeechData& data,std::uint32_t mask,bool automatic_lock)
    : acoustics_(data),automatic_lock_(automatic_lock) {
    if(data.warps().size()!=3 || data.streams().size()<2 || data.streams()[1].parameters[0]!=12)
        throw std::invalid_argument("Unsupported speech branch layout");
    for(const auto& warp:data.warps()) enabled_.push_back((warp.mask&mask)!=0);
    if(std::none_of(enabled_.begin(),enabled_.end(),[](bool value){return value;}))
        throw std::invalid_argument("No active speech warp branch");
    scores_.resize(enabled_.size()); reset();
}
void SpeechBranchSelector::reset() {
    queue_.clear(); std::fill(scores_.begin(),scores_.end(),0.0f);
    mode_=averages_=0; total_weight_=pending_weight_=0; finished_=false;
    locked_=std::count(enabled_.begin(),enabled_.end(),true)==1;
    selected_=unsigned(std::find(enabled_.begin(),enabled_.end(),true)-enabled_.begin());
    if(locked_) mode_=1;
}
std::vector<float> SpeechBranchSelector::emit() {
    if(!locked_ && (mode_==0 || pending_weight_>10)) {
        float best=std::numeric_limits<float>::infinity();
        for(unsigned i=0;i<scores_.size();++i)
            if(enabled_[i] && scores_[i]<best) { best=scores_[i]; selected_=i; }
        pending_weight_=0;
        if(automatic_lock_ && total_weight_>300) { locked_=true; mode_=1; }
    }
    auto result=std::move(queue_.front()[selected_]); queue_.pop_front(); return result;
}
std::vector<std::vector<float>> SpeechBranchSelector::push(const std::vector<std::vector<float>>& cepstra,float weight) {
    if(finished_) throw std::logic_error("Speech branch selector already finished");
    if(cepstra.size()!=enabled_.size() || !std::isfinite(weight) || weight<0 || weight>1)
        throw std::invalid_argument("Invalid speech branch frame");
    for(const auto& branch:cepstra) {
        if(branch.size()!=14) throw std::invalid_argument("Wrong speech cepstrum dimensions");
        for(float value:branch) if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite speech cepstrum");
    }
    std::vector<std::vector<float>> output;
    if((mode_==0 && queue_.size()==50) || (mode_==1 && !queue_.empty())) output.push_back(emit());
    queue_.push_back(cepstra);
    if(!locked_ && weight>std::bit_cast<float>(0x3a83126eu)) {
        total_weight_+=weight; pending_weight_+=weight; averages_=std::min(averages_+1,600u);
        const float rate=1.0f/float(averages_);
        for(unsigned i=0;i<scores_.size();++i) if(enabled_[i]) {
            const float score=acoustics_.centroid_distance(1,std::span(cepstra[i]).subspan(2));
            scores_[i]+=rate*(score*weight-scores_[i]);
        }
    }
    if(mode_==0 && total_weight_>20) mode_=1;
    else if(mode_==2) output.push_back(emit());
    return output;
}
std::vector<std::vector<float>> SpeechBranchSelector::finish() {
    std::vector<std::vector<float>> output;
    if(!locked_ && pending_weight_<=10 && !queue_.empty()) {
        // Event 3 forces selection if a short session never emitted a frame.
        if(mode_==0) pending_weight_=11;
    }
    mode_=1;
    while(!queue_.empty()) output.push_back(emit());
    mode_=2; finished_=true; return output;
}
}
