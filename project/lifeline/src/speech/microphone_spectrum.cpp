#include "speech/microphone_spectrum.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace lifeline {
MicrophoneSpectrum::MicrophoneSpectrum(){
    // ELF 0x1c03d8: preserve the original angle constant and unwindowed PCM scale.
    constexpr float tau=std::bit_cast<float>(std::uint32_t{0x40c90fd0});
    for(unsigned i=0;i<32;++i){const auto angle=(tau/32)*float(i);cosine_[i]=std::cos(angle);sine_[i]=std::sin(angle);}
}
void MicrophoneSpectrum::reset(){input_.fill(0);bands_.fill(0);used_=0;}
void MicrophoneSpectrum::push(std::span<const std::int16_t> pcm){
    for(auto sample:pcm){input_[used_++]=sample;if(used_==32){transform();used_=0;}}
}
void MicrophoneSpectrum::transform(){
    std::array<float,32> real{},imag{};
    for(unsigned i=0;i<32;++i){unsigned reversed=0;for(unsigned bit=0;bit<5;++bit)reversed=(reversed<<1)|((i>>bit)&1);real[reversed]=float(input_[i]);}
    // ELF 0x1c05a8: radix-2 butterflies with both halves of the twiddle table.
    for(unsigned width=2;width<=32;width*=2)for(unsigned base=0;base<32;base+=width)for(unsigned j=0;j<width/2;++j){
        const auto a=base+j,b=a+width/2,k=j*32/width;
        const float ar=real[a],ai=imag[a],br=real[b],bi=imag[b];
        real[a]=(br*cosine_[k]+bi*sine_[k])+ar;imag[a]=(bi*cosine_[k]-br*sine_[k])+ai;
        real[b]=(br*cosine_[k+16]+bi*sine_[k+16])+ar;imag[b]=(bi*cosine_[k+16]-br*sine_[k+16])+ai;
    }
    // ELF 0x1eee50 skips DC; initialize it instead of retaining undefined scratch.
    bands_[0]=0;
    for(unsigned i=1;i<16;++i){const float magnitude=std::sqrt(real[i]*real[i]+imag[i]*imag[i]);bands_[i]=magnitude>1?20*std::log10(magnitude):0;}
}
void MicrophoneSpectrumDisplay::advance(const std::array<float,16>& decibels){
    for(unsigned i=0;i<16;++i){
        const float value=std::isfinite(decibels[i])?std::clamp(decibels[i],0.f,100.f)*.01f:0;
        if(value<=previous_[i]){decay_[i]=std::min(decay_[i]*1.2f,1.f);levels_[i]=std::max(value-value*decay_[i],0.f);}
        else{decay_[i]=.1f;levels_[i]=value;}
        previous_[i]=value;
    }
}
}
