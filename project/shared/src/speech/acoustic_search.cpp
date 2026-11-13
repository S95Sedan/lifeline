#include "speech/acoustic_search.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lifeline {
namespace {
[[noreturn]] void invalid(){throw std::runtime_error("Invalid original acoustic tables or feature frame");}
std::uint32_t u32(std::span<const unsigned char> bytes,std::size_t p){
    if(p>bytes.size() || bytes.size()-p<4)invalid();
    return unsigned(bytes[p]) | unsigned(bytes[p+1])<<8 | unsigned(bytes[p+2])<<16 | unsigned(bytes[p+3])<<24;
}
float real(std::span<const unsigned char> bytes,std::size_t p){
    const auto value=std::bit_cast<float>(u32(bytes,p));if(!std::isfinite(value))invalid();return value;
}
float distance(std::span<const float> x,std::span<const float> y,float best){
    float sum=0;
    for(std::size_t i=0;i<x.size();++i){
        const float delta=x[i]-y[i];const float square=delta*delta;sum+=square;
        if(sum>best)break;
    }
    return sum;
}
}
OriginalSpeechAcoustics::OriginalSpeechAcoustics(const OriginalSpeechData& data)
    :state_streams_(data.state_streams()){
    for(const auto& record:data.streams()){
        const auto& h=record.parameters;
        Stream s{h[0],h[1],h[2],h[3],h[4],h[5],{},{},{},{},{}};
        if(!s.dimensions || s.dimensions>255 || !s.coarse_count || s.coarse_count>255
           || !s.candidates || s.candidates>255 || !s.fine_count || s.fine_count>256
           || s.feature_offset>255 || !s.columns || s.columns>65535)invalid();
        const std::size_t shortlist=4*s.dimensions*s.coarse_count;
        const std::size_t fine=shortlist+4*s.candidates*s.coarse_count;
        const std::size_t packed=fine+4*s.fine_count*(s.dimensions+1);
        const std::size_t packed_size=s.fine_count*((s.columns+1)/2);
        if(packed>record.payload.size() || record.payload.size()-packed!=packed_size)invalid();
        for(std::size_t p=0;p<shortlist;p+=4)s.coarse.push_back(real(record.payload,p));
        for(std::size_t p=shortlist;p<fine;p+=4){
            const auto offset=u32(record.payload,p);
            if(offset%(s.dimensions+1) || offset/(s.dimensions+1)>=s.fine_count)invalid();
            s.shortlist.push_back(offset/(s.dimensions+1));
        }
        for(unsigned i=0;i<s.fine_count;++i){
            const auto start=fine+4*i*(s.dimensions+1);
            for(unsigned j=0;j<s.dimensions;++j)s.fine.push_back(real(record.payload,start+4*j));
            const auto label=u32(record.payload,start+4*s.dimensions);
            if(label>=s.fine_count)invalid();s.labels.push_back(static_cast<std::uint8_t>(label));
        }
        s.packed.assign(record.payload.begin()+packed,record.payload.end());
        feature_count_=std::max(feature_count_,s.feature_offset+s.dimensions);
        streams_.push_back(std::move(s));
    }
    for(const auto& row:state_streams_){
        if(row.size()!=streams_.size())invalid();
        for(std::size_t i=0;i<row.size();++i)if(row[i]>=streams_[i].columns)invalid();
    }
}
std::vector<std::uint8_t> OriginalSpeechAcoustics::quantize(std::span<const float> features)const{
    if(features.size()!=feature_count_)invalid();
    for(float x:features)if(!std::isfinite(x))invalid();
    std::vector<std::uint8_t> result;
    for(const auto& s:streams_){
        const auto x=features.subspan(s.feature_offset,s.dimensions);
        // 0x279a08: first find a coarse centroid, then search its ordered
        // shortlist. Strict '<' keeps the earlier candidate on exact ties.
        // Original 0x7fffffff is an EE positive bound, not a host IEEE NaN.
        float best=std::numeric_limits<float>::infinity();unsigned coarse=0;
        for(unsigned i=0;i<s.coarse_count;++i){
            const auto score=distance(x,std::span(s.coarse).subspan(i*s.dimensions,s.dimensions),best);
            if(score<best){coarse=i;best=score;}
        }
        best=std::numeric_limits<float>::infinity();unsigned label=coarse;
        for(unsigned j=0;j<s.candidates;++j){
            const auto i=s.shortlist[coarse*s.candidates+j];
            const auto score=distance(x,std::span(s.fine).subspan(i*s.dimensions,s.dimensions),best);
            if(score<best){label=s.labels[i];best=score;}
        }
        if(!std::isfinite(best))invalid();
        result.push_back(static_cast<std::uint8_t>(label));
    }
    return result;
}
std::vector<std::uint16_t> OriginalSpeechAcoustics::state_costs(std::span<const std::uint8_t> codes)const{
    if(codes.size()!=streams_.size())invalid();
    for(std::size_t i=0;i<codes.size();++i)if(codes[i]>=streams_[i].fine_count)invalid();
    std::vector<std::uint16_t> result;result.reserve(state_streams_.size());
    for(const auto& row:state_streams_){
        unsigned sum=0;
        for(std::size_t i=0;i<row.size();++i){
            const auto& s=streams_[i];const auto column=row[i];
            const auto byte=s.packed[codes[i]*((s.columns+1)/2)+column/2];
            // 0x27c4b4: even column uses HIGH nibble; odd uses LOW.
            // Preserve the original 16-unit cost scale before summation.
            sum+=(unsigned(byte)<<((column&1)*4))&0xf0;
        }
        result.push_back(static_cast<std::uint16_t>(sum));
    }
    return result;
}
float OriginalSpeechAcoustics::centroid_distance(unsigned stream,std::span<const float> coefficients)const {
    if(stream>=streams_.size()) invalid();
    const auto& s=streams_[stream];
    if(coefficients.size()!=s.dimensions) invalid();
    for(float value:coefficients) if(!std::isfinite(value)) invalid();
    unsigned coarse=0;
    float best=std::numeric_limits<float>::infinity();
    for(unsigned i=0;i<s.coarse_count;++i) {
        const float score=distance(coefficients,std::span(s.coarse).subspan(i*s.dimensions,s.dimensions),best);
        if(score<best) { best=score; coarse=i; }
    }
    best=std::numeric_limits<float>::infinity();
    for(unsigned j=0;j<s.candidates;++j) {
        const auto candidate=s.shortlist[coarse*s.candidates+j];
        const float score=distance(coefficients,std::span(s.fine).subspan(candidate*s.dimensions,s.dimensions),best);
        if(score<best) best=score;
    }
    if(!std::isfinite(best)) invalid();
    return std::sqrt(best);
}
}
