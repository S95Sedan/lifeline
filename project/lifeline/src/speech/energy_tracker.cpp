#include "speech/energy_tracker.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lifeline {
namespace { constexpr float bits(std::uint32_t value) { return std::bit_cast<float>(value); } }
SpeechEnergyTracker::SpeechEnergyTracker(SpeechEnergySettings settings) : settings_(settings) {
    if(settings.warmup>32767 || settings.onset>32767 || settings.hangover>32767 ||
       !settings.level_period || settings.level_period>32767 ||
       !std::isfinite(settings.initial_signal_db) || !std::isfinite(settings.activity_offset_db) ||
       !std::isfinite(settings.noise_scale_parameter) || !std::isfinite(settings.channel_offset))
        throw std::invalid_argument("Invalid speech energy tracker settings");
    constexpr double db_scale=0.23026088558336597;
    activity_offset_=float(double(settings.activity_offset_db)*db_scale);
    initial_signal_=float(double(settings.initial_signal_db)*db_scale-1.0);
    minimum_onset_=settings.noise_scale_parameter*bits(0x3b16e6f6u);
    minimum_onset_=(minimum_onset_+settings.channel_offset)-7.0f;
    minimum_onset_-=1000.0f;
    histogram_floor_=settings.linear_activity ? 6.5f:10.5f;
    for(unsigned i=0;i<bin_energy_.size();++i)
        bin_energy_[i]=histogram_floor_+(float(i)*20.5f)*bits(0x3bdbeb61u);
    reset();
}
void SpeechEnergyTracker::reset() {
    histogram_.fill(0); recent_bins_.fill(0); delayed_energy_.fill(bits(0x5effffffu));
    count_=bin_next_=energy_next_=above_=hangover_=level_frames_=0;
    warmup_=settings_.warmup; initialized_=false;
    signal_=noise_=threshold_=noise_threshold_=0; spread_=1; peak_=bits(0xe0ad78ebu);
}
void SpeechEnergyTracker::set_relative_level(bool relative) {
    if(relative==settings_.relative_level) return;
    if(initialized_) signal_+=relative ? -(spread_+noise_):(spread_+noise_);
    settings_.relative_level=relative;
}
SpeechEnergyFrame SpeechEnergyTracker::push(float energy) {
    if(!std::isfinite(energy) || std::abs(energy)>1000)
        throw std::invalid_argument("Invalid log energy for speech tracker");
    if(!initialized_) {
        spread_=1; noise_=energy;
        signal_=settings_.relative_level ? initial_signal_ : (energy+initial_signal_)+1.0f;
        noise_threshold_=noise_+spread_*1.5f;
        threshold_=float(double(noise_threshold_)+1.1513);
        peak_=bits(0xe0ad78ebu); histogram_.fill(0); count_=0; initialized_=true;
    }
    if(warmup_) {
        --warmup_; initialized_=false;
        return {bits(0x3c23d70au),energy,signal_,threshold_,noise_threshold_,noise_,spread_};
    }
    unsigned rank=7;
    if(count_<40) {
        ++count_;
        rank=unsigned(std::ceil(float(double(float(count_)*bits(0x3e199999u))+0.001)));
    } else --histogram_[recent_bins_[bin_next_]];
    float mapped=(energy-histogram_floor_)*149.0f;
    mapped=mapped*bits(0x3d47ce0cu)+0.5f;
    const auto bin=unsigned(std::clamp(int(mapped),0,149));
    ++histogram_[bin]; recent_bins_[bin_next_]=bin; bin_next_=(bin_next_+1)%40;
    unsigned low=0,high=149,total=histogram_[0];
    while(total<rank && low<149) total+=histogram_[++low];
    total=histogram_[149];
    while(total<rank && high>0) total+=histogram_[--high];
    const float upper=bin_energy_[high];
    const float difference=std::min(bin_energy_[low]-noise_,0.5f);
    noise_+=difference*(difference>0 ? bits(0x3c23d70au):bits(0x3d4cccccu));
    const float delayed=delayed_energy_[energy_next_]; delayed_energy_[energy_next_]=energy;
    energy_next_=(energy_next_+1)%20;
    if(double(delayed-noise_)<double(spread_)+0.5) {
        const float delta=std::min((upper-noise_)-spread_,bits(0x3dccccccu));
        spread_+=delta*(delta>0 ? bits(0x3ca3d70au):bits(0x3e4cccccu));
        if(double(spread_)<0.45999999999999996) spread_=bits(0x3eeb851eu);
    }
    noise_threshold_=spread_*1.5f+noise_;
    const float relative=settings_.relative_level ? spread_+noise_:0;
    const float previous_signal=signal_;
    peak_-=((signal_+relative)-noise_threshold_)*bits(0x3c23d70au);
    if(peak_<upper) peak_=upper-relative;
    threshold_=std::max(float(double(noise_threshold_)+1.1513),minimum_onset_);
    above_=energy>threshold_ ? std::min(above_+1,32767u):0;
    if(above_>settings_.onset) {
        if(!hangover_) peak_=noise_threshold_-relative;
        hangover_=settings_.hangover;
    }
    if(hangover_) {
        --hangover_; ++level_frames_;
        if(level_frames_==settings_.level_period || !hangover_ || peak_>previous_signal+1.0f) {
            const float delta=peak_-previous_signal;
            level_frames_=0;
            signal_=previous_signal+delta*(delta>0 ? bits(0x3e999999u):bits(0x3d4cccccu));
        }
    } else level_frames_=0;
    const float absolute_signal=signal_+relative;
    float activity=0;
    if(settings_.linear_activity) {
        const float upper_threshold=float(std::max(double(absolute_signal-activity_offset_),double(noise_threshold_)+0.5));
        activity=(energy-noise_threshold_)/(upper_threshold-noise_threshold_);
    } else if(energy>noise_threshold_) {
        const float exponent=((energy-absolute_signal)+activity_offset_)*-2.0f;
        activity=1.0f/(std::exp(exponent)+1.0f);
    }
    return {std::clamp(activity,0.0f,1.0f),energy,signal_,threshold_,noise_threshold_,noise_,spread_};
}
}
