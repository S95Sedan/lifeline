#include "speech/filterbank.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lifeline {
SpeechFilterbank::SpeechFilterbank(unsigned bins,unsigned bands,float maximum,float warp,unsigned discard):bins_(bins){
    if(bins<2 || bins>8193 || !bands || bands>256 || discard>256
       || !std::isfinite(maximum) || maximum<=0 || !std::isfinite(warp) || warp<=0)
        throw std::runtime_error("Invalid speech filterbank settings");
    const float step=maximum/float(bins-1);
    const float inv_log_two=std::bit_cast<float>(std::uint32_t{0x3fb8aa3f});
    std::vector<float> coordinate;coordinate.reserve(bins);float frequency=0;
    for(unsigned bin=0;bin<bins;++bin){
        float value;
        if(frequency<1000*warp)value=frequency/(100*warp);
        else {value=std::log(frequency/(1000*warp));value*=inv_log_two;value*=5;value+=10;}
        if(!std::isfinite(value))throw std::runtime_error("Speech filterbank coordinate overflow");
        coordinate.push_back(value);frequency+=step;
    }
    for(unsigned band=0;band<bands;++band){
        const float center=float(discard+band+1);Band out{0,{}};
        // Original excludes DC and both exact triangle endpoints.
        for(unsigned bin=1;bin<bins;++bin){
            const float x=coordinate[bin];if(x<=center-1 || x>=center+1)continue;
            if(out.weights.empty())out.first=bin;
            const float weight=x<center?(1-center)+x:(1-x)+center;
            out.weights.push_back(weight);
        }
        bands_.push_back(std::move(out));
    }
}
std::vector<float> SpeechFilterbank::transform(std::span<const float> power)const{
    if(power.size()!=bins_)throw std::runtime_error("Incomplete speech power spectrum");
    for(float value:power)if(!std::isfinite(value) || value<0)throw std::runtime_error("Invalid speech power spectrum");
    std::vector<float> result;result.reserve(bands_.size());
    for(const auto& band:bands_){
        float sum=10; // Authored additive floor, not max(sum, 10).
        for(unsigned i=0;i<band.weights.size();++i){const float product=power[band.first+i]*band.weights[i];sum+=product;}
        if(!std::isfinite(sum))throw std::runtime_error("Speech filterbank overflow");
        result.push_back(sum);
    }
    return result;
}
}
