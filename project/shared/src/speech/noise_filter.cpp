#include "speech/noise_filter.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace lifeline {
namespace {float constant(std::uint32_t bits){return std::bit_cast<float>(bits);}}
SpeechNoiseFilter::SpeechNoiseFilter(NoiseFilterSettings settings):settings_(std::move(settings)){
    const auto& s=settings_;
    if(!s.bands || s.bands>256 || s.noise_delay>1024 || s.energy_start>=s.bands
       || !std::isfinite(s.smoothing) || s.smoothing<0 || s.smoothing>1
       || !std::isfinite(s.noise_scale) || s.noise_scale<=0
       || !std::isfinite(s.minimum_gain) || s.minimum_gain<=0 || s.minimum_gain>1
       || !std::isfinite(s.averaging_limit) || s.averaging_limit<1 || s.averaging_limit>65534)
        throw std::runtime_error("Invalid speech noise filter settings");
    unsigned previous=0;
    for(unsigned end:s.region_ends){if(end<=previous || end>s.bands)throw std::runtime_error("Invalid speech noise regions");previous=end;}
    if(previous!=s.bands)throw std::runtime_error("Incomplete speech noise regions");
    history_.resize(s.noise_delay+1,std::vector<float>(s.bands));
    noise_.resize(s.bands);log_noise_.resize(s.bands);prior_.resize(s.bands);ratio_.resize(s.bands);gain_.resize(s.bands);
    reset();
}
void SpeechNoiseFilter::reset(){
    next_=quiet_=averages_=0;activity_=1;
    for(auto& frame:history_)std::fill(frame.begin(),frame.end(),0.0f);
    for(auto* array:{&noise_,&log_noise_,&prior_,&ratio_,&gain_})std::fill(array->begin(),array->end(),1.0f);
}
NoiseFilterFrame SpeechNoiseFilter::push(std::span<const float> bands,bool active){
    const auto& s=settings_;
    if(bands.size()!=s.bands)throw std::runtime_error("Incomplete speech noise frame");
    for(float value:bands)if(!std::isfinite(value) || value<=0)throw std::runtime_error("Invalid speech band power");
    std::copy(bands.begin(),bands.end(),history_[next_].begin());
    next_=(next_+1)%unsigned(history_.size());
    if(active)quiet_=0;
    else {
        if(!averages_)for(unsigned i=0;i<s.bands;++i){noise_[i]=bands[i];log_noise_[i]=0.5f*std::log(bands[i]);}
        if(quiet_<=s.noise_delay)++quiet_;
        if(quiet_==s.noise_delay+1){
            const float divisor=float(averages_)>=s.averaging_limit?s.averaging_limit:float(++averages_);
            const float weight=1.0f/divisor;
            for(unsigned i=0;i<s.bands;++i){
                const float difference=std::min(history_[next_][i]-noise_[i],noise_[i]);
                const float correction=weight*difference;noise_[i]+=correction;
                log_noise_[i]=0.5f*std::log(noise_[i]);
            }
        }
    }
    if(averages_){
        const float complement=1.0f-s.smoothing,epsilon=constant(0x358637bd);
        for(unsigned i=0;i<s.bands;++i){
            const float product=ratio_[i]*gain_[i];prior_[i]=s.smoothing*(product+1.0f);
        }
        for(unsigned i=0;i<s.bands;++i){
            const float scaled=s.noise_scale*noise_[i];ratio_[i]=(bands[i]+epsilon)/(scaled+epsilon);
        }
        for(unsigned i=0;i<s.bands;++i){const float contribution=complement*ratio_[i];prior_[i]+=contribution;}
    }
    float peak=0;unsigned begin=0;
    for(unsigned end:s.region_ends){
        float sum=0;for(unsigned i=begin;i<end;++i)sum+=ratio_[i];
        peak=std::max(peak,sum/float(end-begin));begin=end;
    }
    const float scaled_peak=peak*constant(0x3f68ba2e);
    const float exponent=std::min(-std::log(11.0f)+scaled_peak,std::log(constant(0x7de38dd0)));
    const float numerator=constant(0x3e4ccccc)+constant(0x3f666666)*activity_;
    const float denominator=constant(0x3f4ccccc)+constant(0x3dcccccc)*activity_;
    activity_=(numerator/denominator)*std::exp(exponent);
    if(!averages_)std::fill(gain_.begin(),gain_.end(),1.0f);
    else for(unsigned i=0;i<s.bands;++i){
        const float gain=prior_[i]>1?1.0f-1.0f/prior_[i]:0;
        gain_[i]=std::max(gain*gain,s.minimum_gain); // EE MAX.S at 0x276f44.
    }
    NoiseFilterFrame result;result.log_bands.resize(s.bands);result.log_noise=log_noise_;float energy=0;
    for(unsigned i=0;i<s.bands;++i){
        const float filtered=gain_[i]*bands[i];result.log_bands[i]=0.5f*std::log(filtered);
        if(i>=s.energy_start)energy+=filtered;
    }
    result.log_energy=std::log(energy);result.log_activity=std::log(activity_);
    return result;
}
}
