#include "speech/noise_model.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline {
namespace {
constexpr float range=-std::bit_cast<float>(0xc11391a7u);
constexpr float rate=std::bit_cast<float>(0x3b03126eu);
}
SpeechNoiseModel::SpeechNoiseModel(const OriginalSpeechData& data,unsigned interval) : interval_(interval) {
    if(!interval || interval>255) throw std::invalid_argument("Invalid speech noise adaptation interval");
    const auto row=data.external_acoustic_row();
    for(std::size_t s=0;s<data.streams().size();++s) {
        const auto& stream=data.streams()[s]; const auto& h=stream.parameters;
        const std::size_t start=4*((std::size_t(h[0])+h[2])*h[1]+(std::size_t(h[0])+1)*h[3]);
        const unsigned stride=(h[5]+1)/2;
        if(row>=data.state_streams().size() || s>=data.state_streams()[row].size() ||
           !h[3] || h[3]>256 || !h[5] || start+std::size_t(stride)*h[3]!=stream.payload.size())
            throw std::invalid_argument("Invalid speech noise model stream");
        const auto column=data.state_streams()[row][s];
        if(column>=h[5]) throw std::invalid_argument("Invalid speech noise model column");
        std::vector<std::uint8_t> values(h[3]);
        for(unsigned code=0;code<h[3];++code) {
            const auto packed=stream.payload[start+std::size_t(code)*stride+column/2];
            values[code]=std::uint8_t(((packed>>(column%2 ? 0:4))&15)*17);
        }
        initial_.push_back(std::move(values));
    }
    probabilities_.resize(256);
    for(unsigned i=0;i<256;++i) probabilities_[i]=std::exp(float(-double(float(i)*range)/255.0));
    updates_.resize(std::size_t(interval+1)*256);
    const float log_denominator=std::log(float(1.0+double(float(interval)*rate)));
    for(unsigned count=0;count<=interval;++count) for(unsigned cost=0;cost<256;++cost) {
        const float log_probability=std::log(probabilities_[cost]+float(count)*rate)-log_denominator;
        const float mapped=float(double(log_probability)*-255.0/double(range)+0.5);
        updates_[std::size_t(count)*256+cost]=std::uint8_t(std::clamp(mapped,0.0f,255.0f));
    }
    reset();
}
void SpeechNoiseModel::reset() {
    costs_=initial_; counts_=initial_;
    for(auto& counts:counts_) std::fill(counts.begin(),counts.end(),std::uint8_t(0));
    quiet_=updates_since_normalization_=0;
}
std::uint16_t SpeechNoiseModel::push(std::span<const std::uint8_t> codes,bool active) {
    if(codes.size()!=costs_.size()) throw std::invalid_argument("Wrong speech noise model code count");
    for(std::size_t s=0;s<codes.size();++s)
        if(codes[s]>=costs_[s].size()) throw std::invalid_argument("Invalid speech noise model code");
    bool update=false;
    if(!active && ++quiet_==interval_) { quiet_=0; update=true; ++updates_since_normalization_; }
    unsigned cost=0;
    for(std::size_t s=0;s<codes.size();++s) {
        if(!active) ++counts_[s][codes[s]];
        if(update) {
            for(std::size_t i=0;i<costs_[s].size();++i) {
                costs_[s][i]=updates_[std::size_t(counts_[s][i])*256+costs_[s][i]];
                counts_[s][i]=0;
            }
            if(updates_since_normalization_==20) {
                float sum=0; for(auto value:costs_[s]) sum+=probabilities_[value];
                const float correction=float(double(std::log(sum))*255.0/double(range));
                const int shift=int(double(correction)+(correction<0 ? -0.5:0.5));
                for(auto& value:costs_[s]) value=std::uint8_t(std::clamp(int(value)+shift,0,255));
            }
        }
        cost+=costs_[s][codes[s]];
    }
    if(updates_since_normalization_==20) updates_since_normalization_=0;
    return std::uint16_t(cost);
}
}
