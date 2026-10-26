#include "speech/audio.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lifeline {
namespace {
constexpr std::size_t frame=320;
std::vector<double> levels(std::span<const std::int16_t> audio){
    std::vector<double> out;
    for(std::size_t p=0;p<audio.size();p+=frame){double sum=0;const auto chunk=audio.subspan(p,std::min(frame,audio.size()-p));for(auto s:chunk)sum+=double(s)*s;out.push_back(std::sqrt(sum/chunk.size()));}
    return out;
}
}
std::vector<std::int16_t> stretch_speech_audio(std::span<const std::int16_t> input,float factor){
    if(!std::isfinite(factor) || factor<1 || factor>2 || input.size()>160000)throw std::runtime_error("Invalid speech time expansion");
    if(input.size()<640 || factor==1)return {input.begin(),input.end()};
    // Waveform-similarity overlap-add: expand duration without lowering pitch.
    // 40 ms frames, 20 ms output hop, +/-5 ms alignment search at 16 kHz.
    constexpr std::size_t hop=320,window=640,search=80;
    const auto size=std::size_t(std::lround(input.size()*double(factor)));
    std::vector<float> output(size+window,0);
    std::copy_n(input.begin(),window,output.begin());
    for(std::size_t p=hop;p<size;p+=hop){
        const auto center=std::min(std::size_t(double(p)/factor),input.size()-window);
        const auto first=center>search?center-search:0,last=std::min(center+search,input.size()-window);
        std::size_t selected=center;double best=-2,existing_energy=0;
        for(std::size_t i=0;i<hop;i+=2)existing_energy+=double(output[p+i])*output[p+i];
        for(auto candidate=first;candidate<=last;++candidate){double dot=0,energy=0;
            for(std::size_t i=0;i<hop;i+=2){const auto s=input[candidate+i];dot+=double(output[p+i])*s;energy+=double(s)*s;}
            const double similarity=dot/std::sqrt(std::max(1.,existing_energy*energy))-.00001*std::abs(double(candidate)-double(center));
            if(similarity>best){best=similarity;selected=candidate;}
        }
        for(std::size_t i=0;i<hop;++i){const float mix=float(i)/float(hop);output[p+i]=output[p+i]*(1-mix)+input[selected+i]*mix;}
        for(std::size_t i=hop;i<window;++i)output[p+i]=input[selected+i];
    }
    std::vector<std::int16_t> result(size);
    for(std::size_t i=0;i<size;++i)result[i]=static_cast<std::int16_t>(std::clamp(std::lround(output[i]),-32768l,32767l));
    return result;
}
std::vector<std::int16_t> prepare_speech_audio(std::span<const std::int16_t> input){
    // A DC blocker attenuates handling/room rumble without a hard noise gate
    // that would erase quiet consonants. Gain is fixed over the utterance.
    std::vector<double> filtered;filtered.reserve(input.size());double old_in=0,old_out=0,peak=1;
    constexpr double pole=.9767134703747672; // exp(-2*pi*60/16000)
    for(auto s:input){const double v=double(s)-old_in+pole*old_out;old_in=s;old_out=v;filtered.push_back(v);peak=std::max(peak,std::abs(v));}
    // Raise quiet recordings, but do not amplify silence into speech.
    const double gain=peak>=32?std::clamp(12000/peak,.125,12.0):1.0;
    std::vector<std::int16_t> out;out.reserve(input.size());
    for(auto v:filtered)out.push_back(static_cast<std::int16_t>(std::clamp(std::lround(v*gain),-32768l,32767l)));
    return out;
}
bool speech_covers_capture(std::span<const std::int16_t> audio,std::size_t first,std::size_t last){
    if(first>=last || last>audio.size())return false;
    const auto energy=levels(audio);auto ordered=energy;if(ordered.empty())return false;
    std::sort(ordered.begin(),ordered.end());
    // Estimate ambient level from the recording, not the artificial zero
    // padding used to finalize SAPI. Constant room noise is not another word.
    const double floor=ordered[ordered.size()/5],peak=ordered.back();
    const double threshold=std::max({60.0,floor*3.5,peak*.075});
    // An interval cannot cover speech when no frame rises above the same
    // ambient threshold. Otherwise stationary noise passes this check
    // vacuously and can turn an acoustic false match into a game command.
    if(peak<=threshold)return false;
    constexpr std::size_t edge=1280; // 80 ms accommodates short-word endpoints.
    first=first>edge?first-edge:0;last=std::min(audio.size(),last+edge);
    unsigned consecutive=0;
    for(std::size_t i=0;i<energy.size();++i){const auto p=i*frame;
        if((p+frame<=first || p>=last) && energy[i]>threshold){if(++consecutive>=4)return false;}
        else consecutive=0;
    }
    return true; // Brief clicks/breaths do not discard an otherwise valid word.
}
}
