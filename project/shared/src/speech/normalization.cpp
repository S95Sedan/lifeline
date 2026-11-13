#include "speech/normalization.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lifeline {
SpeechNormalization::SpeechNormalization(SpeechNormalizationData data, SpeechNormalizationSettings settings)
    : data_(std::move(data)), settings_(settings), projection_(data_.projection) {
    const auto bands=data_.projection.bands;
    if(data_.initial_baseline.size()!=bands || data_.band_floor.size()!=bands || data_.soft_floor.size()!=353 ||
       settings.queue_capacity<2 || settings.queue_capacity>4096 || settings.quiet_drain>4096 ||
       !std::isfinite(settings.adaptation_limit) || settings.adaptation_limit<=0 ||
       !std::isfinite(settings.initial_weight) || settings.initial_weight<0 ||
       !std::isfinite(settings.noise_offset) || !std::isfinite(settings.difference_floor) ||
       !std::isfinite(settings.energy_floor))
        throw std::invalid_argument("Invalid speech normalization data or settings");
    for(const auto* values:{&data_.initial_baseline,&data_.band_floor,&data_.soft_floor})
        for(float value:*values)
            if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite speech normalization data");
    reset();
}
void SpeechNormalization::reset() {
    queue_.clear(); mode_=2; quiet_=0; weight_=settings_.initial_weight;
    shift_weight_=0.5f; dirty_=0; energy_reference_=100000; finished_=false;
    valid_=weight_>0;
    mean_=valid_ ? data_.initial_baseline : std::vector<float>(data_.projection.bands,0);
    baseline_=valid_ ? projection_.project(mean_) : mean_;
}
float SpeechNormalization::soft_floor(float difference) const {
    // 0x274af8 has two resolutions and rounds the nonnegative table index.
    if(difference < -9.5f) return 0;
    const auto index=unsigned(difference> -0.5f ? double(difference)*128.0+352.5 : double(difference)*32.0+304.5);
    return data_.soft_floor.at(index);
}
SpeechNormalizedFrame SpeechNormalization::emit() {
    if(dirty_>10.0f/settings_.adaptation_limit) {
        baseline_=projection_.project(mean_); dirty_=0; valid_=true;
    }
    auto output=std::move(queue_.front()); queue_.pop_front();
    for(std::size_t i=0;i<output.bands.size();++i) {
        const float floor=data_.band_floor[i];
        if(valid_) {
            const float value=output.bands[i]-baseline_[i];
            output.bands[i]=std::max(value,floor)+soft_floor(-std::abs(value-floor));
        } else output.bands[i]=floor;
    }
    const float energy=output.energy-energy_reference_,floor=settings_.energy_floor;
    const float correction=soft_floor(-std::abs(energy-floor)*0.5f);
    output.energy=float(double(std::max(energy,floor))+2.0*double(correction));
    return output;
}
std::vector<SpeechNormalizedFrame> SpeechNormalization::push(std::span<const float> bands,
    std::span<const float> noise,float weight,float energy,float energy_reference) {
    if(finished_) throw std::logic_error("Speech normalization already finished");
    if(bands.size()!=mean_.size() || noise.size()!=mean_.size() || !std::isfinite(weight) || weight<0 ||
       !std::isfinite(energy) || !std::isfinite(energy_reference))
        throw std::invalid_argument("Invalid speech normalization frame");
    for(std::size_t i=0;i<bands.size();++i)
        if(!std::isfinite(bands[i]) || !std::isfinite(noise[i]))
            throw std::invalid_argument("Nonfinite speech normalization frame");
    std::vector<SpeechNormalizedFrame> output;
    if(weight_>=settings_.adaptation_limit) {
        mode_=0;
        while(!queue_.empty()) output.push_back(emit());
    }
    energy_reference_=energy_reference;
    queue_.push_back({energy,{bands.begin(),bands.end()}});
    constexpr float activity_threshold=std::bit_cast<float>(0x3a83126eu);
    if(weight>activity_threshold) {
        weight_=std::min(weight_+weight,settings_.adaptation_limit);
        const float rate=weight/weight_;
        dirty_+=rate;
        const float fast=std::min(rate*4.0f,1.0f), slow=std::min(rate/4.0f,1.0f);
        float residual_sum=0;
        for(std::size_t i=0;i<bands.size();++i) {
            const float target=std::max(bands[i],noise[i]+settings_.noise_offset);
            const float difference=std::max(target-mean_[i],settings_.difference_floor);
            mean_[i]+=difference*(difference>0 ? fast:slow);
            const float residual=target-mean_[i];
            residual_sum+=residual>0 ? residual*4.0f : residual/4.0f;
        }
        shift_weight_=std::min(shift_weight_+weight,200.0f);
        const float shift_rate=weight/shift_weight_;
        dirty_+=shift_rate;
        const float shift=std::clamp(residual_sum*(shift_rate/float(bands.size())),-0.7f,0.7f);
        for(auto& value:mean_) value+=shift;
    }
    quiet_=weight==0 ? std::min(quiet_+1,settings_.quiet_drain+1) : 0;
    if(mode_==0) output.push_back(emit());
    else if(mode_==2) {
        if(weight>activity_threshold) mode_=1;
        else output.push_back(emit());
    } else if(quiet_>settings_.quiet_drain) {
        mode_=2;
        while(!queue_.empty()) output.push_back(emit());
    } else if(queue_.size()==settings_.queue_capacity) output.push_back(emit());
    return output;
}
std::vector<SpeechNormalizedFrame> SpeechNormalization::finish() {
    std::vector<SpeechNormalizedFrame> output;
    while(!queue_.empty()) output.push_back(emit());
    finished_=true;
    return output;
}
}
