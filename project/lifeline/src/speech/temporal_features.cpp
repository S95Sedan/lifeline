#include "speech/temporal_features.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lifeline {
OriginalSpeechFeatures::OriginalSpeechFeatures(unsigned coefficients):coefficients_(coefficients){
    if(!coefficients || coefficients>255)throw std::runtime_error("Invalid original temporal feature dimensions");
    for(auto& slot:ring_)slot.resize(3+3*coefficients);
    reset();
}
void OriginalSpeechFeatures::reset(){
    next_=0;frames_=0;finished_=false;
    for(auto& slot:ring_)std::fill(slot.begin(),slot.end(),0.0f);
}
std::vector<float> OriginalSpeechFeatures::output(unsigned index,std::size_t phase)const{
    const auto& slot=ring_[index];
    std::vector<float> result(slot.begin()+1,slot.end());
    // Startup/drain branches at 0x27af40. No derivative padding is guessed.
    if(phase<7){
        result[1]=0;
        std::fill(result.begin()+2+2*coefficients_,result.end(),0.0f);
        if(phase<6){
            result[0]=0;
            std::fill(result.begin()+2+coefficients_,result.begin()+2+2*coefficients_,0.0f);
        }
    }
    return result;
}
std::optional<std::vector<float>> OriginalSpeechFeatures::push(std::span<const float> input){
    if(finished_ || input.size()!=coefficients_+2)throw std::runtime_error("Incomplete or closed original cepstral stream");
    for(float x:input)if(!std::isfinite(x))throw std::runtime_error("Non-finite original cepstral stream");
    auto& current=ring_[next_];const auto& previous=ring_[(next_+7)&7];
    auto& center=ring_[(next_+6)&7];auto& delayed=ring_[(next_+5)&7];
    const auto& old=ring_[(next_+4)&7];
    current[0]=input[0];
    float energy=current[0]+current[0];energy+=previous[0];energy-=delayed[0];
    const float twice_old=old[0]+old[0];energy-=twice_old;
    center[1]=energy*std::bit_cast<float>(std::uint32_t(0x3e4ccccc));
    delayed[2]=center[1]-old[1];
    for(unsigned i=0;i<coefficients_;++i){
        current[3+i]=input[2+i];
        float delta=current[3+i]+current[3+i];delta+=previous[3+i];delta-=delayed[3+i];
        const float twice=old[3+i]+old[3+i];delta-=twice;
        center[3+coefficients_+i]=delta*0.375f;
    }
    for(unsigned i=0;i<coefficients_;++i)
        delayed[3+2*coefficients_+i]=center[3+coefficients_+i]-old[3+coefficients_+i];
    ++frames_;
    std::optional<std::vector<float>> result;
    if(frames_>=4)result=output((next_+5)&7,frames_);
    next_=(next_+1)&7;
    return result;
}
std::vector<std::vector<float>> OriginalSpeechFeatures::finish(){
    std::vector<std::vector<float>> result;
    if(finished_)return result;
    finished_=true;if(!frames_)return result;
    // 0x27b238 / 0x27aedc: bounded drain countdown and ring advancement.
    std::size_t phase=frames_<3?frames_+4:7;
    do {
        --phase;result.push_back(output((next_+5)&7,phase));next_=(next_+1)&7;
    }while(phase>4);
    return result;
}
}
