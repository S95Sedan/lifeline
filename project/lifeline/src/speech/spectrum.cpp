#include "speech/spectrum.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace lifeline {
SpeechSpectrum::SpeechSpectrum(unsigned input,unsigned size,unsigned first,unsigned last,float floor)
    :size_(size),input_size_(input),first_(first),last_(last),energy_floor_(floor){
    if(size<4 || size>16384 || (size&(size-1)) || !input || input>size || first>last
       || last>=size/2 || !std::isfinite(floor) || floor<=0)
        throw std::runtime_error("Invalid speech spectrum settings");
    for(unsigned k=0;k<size/2;++k){
        const double angle=-2*std::numbers::pi*k/size;
        rotations_.emplace_back(float(std::cos(angle)),float(std::sin(angle)));
    }
}
SpeechSpectrumFrame SpeechSpectrum::transform(std::span<const float> input)const{
    if(input.size()!=input_size_)throw std::runtime_error("Incomplete speech window");
    std::vector<std::complex<float>> values(size_);
    for(unsigned i=0;i<input_size_;++i){
        if(!std::isfinite(input[i]))throw std::runtime_error("Non-finite speech window");
        values[i]=input[i];
    }
    for(unsigned i=1,j=0;i<size_;++i){
        unsigned bit=size_/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(values[i],values[j]);
    }
    for(unsigned width=2;width<=size_;width*=2){
        const unsigned half=width/2,stride=size_/width;
        for(unsigned start=0;start<size_;start+=width)for(unsigned k=0;k<half;++k){
            const auto a=values[start+k],b=values[start+k+half],w=rotations_[k*stride];
            const float rr=b.real()*w.real(),ii=b.imag()*w.imag();
            const float ri=b.real()*w.imag(),ir=b.imag()*w.real();
            const std::complex<float> rotated{rr-ii,ri+ir};
            values[start+k]=a+rotated;values[start+k+half]=a-rotated;
        }
    }
    SpeechSpectrumFrame result;result.power.resize(size_/2+1);float energy=energy_floor_;
    // 0x2669a0: DC/Nyquist have no imaginary component. Interior bins are
    // squared magnitudes; sum the inclusive energy interval before taking ln.
    for(unsigned k=0;k<=size_/2;++k){
        const float re=values[k].real(),im=(k==0 || k==size_/2)?0:values[k].imag();
        const float r2=re*re,i2=im*im;const float power=r2+i2;
        if(!std::isfinite(power))throw std::runtime_error("Speech spectrum overflow");
        result.power[k]=power;if(k>=first_ && k<=last_)energy+=power;
    }
    if(!std::isfinite(energy))throw std::runtime_error("Speech energy overflow");
    result.log_energy=std::log(energy);return result;
}
}
