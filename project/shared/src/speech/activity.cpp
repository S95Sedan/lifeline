#include "speech/activity.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline {
SpeechActivityGate::SpeechActivityGate(unsigned delay,unsigned window,float threshold)
    : delay_(delay),threshold_(threshold) {
    if(!window || window>4096 || delay>window || !std::isfinite(threshold))
        throw std::invalid_argument("Invalid speech activity gate settings");
    history_.resize(window);
}
void SpeechActivityGate::reset() {
    std::fill(history_.begin(),history_.end(),0.0f); sum_=0; next_=pending_=0; finished_=false;
}
std::optional<bool> SpeechActivityGate::push(float activity) {
    if(finished_) throw std::logic_error("Speech activity gate already finished");
    if(!std::isfinite(activity) || activity<0 || activity>1)
        throw std::invalid_argument("Invalid speech activity value");
    sum_-=history_[next_]; history_[next_]=activity; sum_+=activity;
    next_=(next_+1)%unsigned(history_.size());
    if(pending_==delay_) return sum_>threshold_;
    ++pending_; return {};
}
std::vector<bool> SpeechActivityGate::finish() {
    // The original repeats the current decision; it does not add silence to
    // the sum when releasing the final delayed decisions.
    std::vector<bool> output(pending_,sum_>threshold_);
    pending_=0; finished_=true; return output;
}
SpeechAdaptationWeight::SpeechAdaptationWeight(unsigned radius,float threshold)
    : radius_(radius),threshold_(threshold) {
    if(!radius || radius>2048 || !std::isfinite(threshold) || threshold<0)
        throw std::invalid_argument("Invalid speech adaptation weight settings");
    energy_.resize(2*std::size_t(radius)+1); weights_.resize(radius);
}
void SpeechAdaptationWeight::reset() {
    std::fill(energy_.begin(),energy_.end(),0.0f); std::fill(weights_.begin(),weights_.end(),0.0f);
    pending_=energy_next_=weight_next_=0; finished_=false;
}
float SpeechAdaptationWeight::evaluate(float weight) const {
    if(weight<=0) return weight;
    const auto [low,high]=std::minmax_element(energy_.begin(),energy_.end());
    return *high-*low<threshold_ ? weight : weight*std::bit_cast<float>(0x3dccccccu);
}
std::optional<float> SpeechAdaptationWeight::push(float energy,float weight) {
    if(finished_) throw std::logic_error("Speech adaptation weight already finished");
    if(!std::isfinite(energy) || !std::isfinite(weight) || weight<0 || weight>1)
        throw std::invalid_argument("Invalid speech adaptation input");
    if(pending_==0) { std::fill(energy_.begin(),energy_.end(),energy); energy_next_=0; }
    else { energy_[energy_next_]=energy; energy_next_=(energy_next_+1)%unsigned(energy_.size()); }
    ++pending_;
    const float delayed=weights_[weight_next_]; weights_[weight_next_]=weight;
    weight_next_=(weight_next_+1)%radius_;
    if(pending_<=radius_) return {};
    --pending_; return evaluate(delayed);
}
std::vector<float> SpeechAdaptationWeight::finish() {
    std::vector<float> output;
    while(pending_) {
        output.push_back(evaluate(weights_[weight_next_])); weights_[weight_next_]=0;
        weight_next_=(weight_next_+1)%radius_; --pending_;
    }
    finished_=true; return output;
}
}
