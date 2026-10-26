#include "speech/pcm_window.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline {
OriginalSpeechWindow::OriginalSpeechWindow(unsigned hop,unsigned length):hop_(hop),length_(length){
    if(!hop || hop>16384 || length<2 || length<hop || length>3*hop || (length-hop)%2)
        throw std::runtime_error("Unsupported original speech window geometry");
    // 0x27aab4..0x27abcc; preserve the stored float constants and the
    // double angle constant at 0x341ee0 before the float cosine argument.
    const float a=std::bit_cast<float>(std::uint32_t(0x3f0a3d70));
    const float b=std::bit_cast<float>(std::uint32_t(0x3eeb851e));
    for(unsigned i=0;i<length;++i){
        const float angle=static_cast<float>(double(i)*6.283185307999999/double(length-1));
        const float product=b*std::cos(angle);weights_.push_back(a-product);
    }
    reset();
}
void OriginalSpeechWindow::reset(){
    ring_.clear();ring_.reserve(3*hop_);last_block_.clear();
    previous_=first_=last_=0;started_=finished_=false;
}
std::vector<float> OriginalSpeechWindow::append(std::span<const float> block){
    if(ring_.size()==3*hop_)ring_.erase(ring_.begin(),ring_.begin()+hop_);
    ring_.insert(ring_.end(),block.begin(),block.end());
    if(ring_.size()!=3*hop_)throw std::runtime_error("Original speech window is not primed");
    const auto start=(3*hop_-length_)/2;
    std::vector<float> result(length_);
    for(unsigned i=0;i<length_;++i)result[i]=ring_[start+i]*weights_[i];
    return result;
}
std::optional<std::vector<float>> OriginalSpeechWindow::push_block(std::span<const std::int16_t> pcm){
    if(finished_ || pcm.size()!=hop_)throw std::runtime_error("Original speech requires a complete input block");
    std::vector<float> block;block.reserve(hop_);
    // 0x27a490: first difference, with history across input blocks.
    for(auto sample:pcm){block.push_back(float(sample)-float(previous_));previous_=sample;}
    first_=pcm.front();last_=pcm.back();
    if(!started_){
        // Original warm-up copies the first block, zeroing its first delta.
        // It does not prepend invented silent PCM.
        block[0]=0;ring_=block;ring_.insert(ring_.end(),block.begin(),block.end());
        last_block_=std::move(block);started_=true;return {};
    }
    last_block_=block;return append(block);
}
std::optional<std::vector<float>> OriginalSpeechWindow::finish(){
    if(finished_ || !started_)return {};
    finished_=true;
    // 0x27a0b0: repeat the last block and replace its first difference with
    // first-sample minus last-sample at the repeated block boundary.
    auto repeated=last_block_;repeated[0]=float(first_)-float(last_);
    return append(repeated);
}
}
