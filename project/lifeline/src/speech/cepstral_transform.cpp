#include "speech/cepstral_transform.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace lifeline {
OriginalSpeechCepstrum::OriginalSpeechCepstrum(unsigned bands,unsigned coefficients,unsigned lifter_period,unsigned discard)
    :bands_(bands),coefficients_(coefficients),discard_(discard),scale_(0){
    if(!bands || bands>512 || !coefficients || coefficients>512 || !lifter_period || lifter_period>65535 || discard>=bands)
        throw std::runtime_error("Invalid original cepstral transform settings");
    const auto count=bands-discard;scale_=static_cast<float>(2.0/double(count));
    for(unsigned k=0;k<coefficients;++k){
        const float angle=static_cast<float>(double(k)*std::numbers::pi/double(lifter_period));
        const float weighted=6.0f*std::sin(angle);const float lifter=1.0f+weighted;
        for(unsigned j=0;j<count;++j){
            const float argument=static_cast<float>((double(j)+0.5)*std::numbers::pi*double(k)/double(count));
            matrix_.push_back(lifter*std::cos(argument));
        }
    }
}
std::vector<float> OriginalSpeechCepstrum::transform(std::span<const float> input)const{
    if(input.size()!=bands_+1)throw std::runtime_error("Incomplete original spectral-band frame");
    for(float x:input)if(!std::isfinite(x))throw std::runtime_error("Non-finite original spectral-band frame");
    const auto count=bands_-discard_;
    std::vector<float> result(coefficients_+1);result[0]=input[0];
    for(unsigned k=0;k<coefficients_;++k){
        float sum=0;
        for(unsigned j=0;j<count;++j){const float product=matrix_[k*count+j]*input[1+discard_+j];sum+=product;}
        result[k+1]=sum*scale_;
    }
    result[1]*=0.5f;
    return result;
}
}
