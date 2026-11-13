#include "game/action_table.hpp"
#include <bit>
#include <limits>
#include <stdexcept>

namespace lifeline::script {
unsigned ActionTable::word(std::size_t offset)const{
    if(offset>bytes_.size() || bytes_.size()-offset<4)throw std::runtime_error("Action table extent outside installed resource");
    return le32(bytes_.data()+offset);
}
int ActionTable::number(std::size_t offset)const{return std::bit_cast<int>(word(offset));}
void ActionTable::load(Bytes bytes){
    // Validate structural roots before replacing the active table. Offsets in
    // each index are relative to that index, not to the file start.
    ActionTable candidate;candidate.bytes_=std::move(bytes);
    candidate.modes_=candidate.word(4);
    candidate.segments_=std::size_t(candidate.word(0))*4;
    candidate.segment_count_=candidate.word(candidate.segments_);candidate.segments_+=4;
    if(candidate.modes_>candidate.bytes_.size()/4 || candidate.segment_count_>candidate.bytes_.size()/4)throw std::runtime_error("Invalid action table index count");
    candidate.word(8+std::size_t(candidate.modes_)*4);
    candidate.word(candidate.segments_+std::size_t(candidate.segment_count_)*4);
    bytes_=std::move(candidate.bytes_);segments_=candidate.segments_;modes_=candidate.modes_;segment_count_=candidate.segment_count_;
}
std::optional<int> ActionTable::lookup(std::size_t base,unsigned index,int command,int mode,bool segment)const{
    const auto offset=word(base+std::size_t(index)*4);if(!offset)return {};
    const auto list=base+std::size_t(offset)*4;const auto count=word(list);
    const auto stride=segment?12u:8u;
    if(count>bytes_.size()/stride)throw std::runtime_error("Invalid action table record count");
    for(unsigned pass=0;pass<(segment?2u:1u);++pass)for(unsigned i=0;i<count;++i){
        const auto p=list+4+std::size_t(i)*stride;
        if(number(p)!=command || (segment && number(p+4)!=(pass?-1:mode)))continue;
        const int value=number(p+(segment?8:4));
        const auto result=std::int64_t(value)+(segment || value<100000?scene_base_:std::int64_t(shared_base_)-100000);
        if(result<std::numeric_limits<int>::min() || result>std::numeric_limits<int>::max())throw std::runtime_error("Action event offset overflow");
        return int(result);
    }
    return {};
}
int ActionTable::event(int mode,int segment,int command)const{
    if(command==-2)return shared_base_;
    if(bytes_.empty())throw std::runtime_error("Action table is not loaded");
    // Original permits index == segment_count (the last/default index).
    if(segment>=0 && unsigned(segment)<=segment_count_)if(auto match=lookup(segments_,unsigned(segment),command,mode,true))return *match;
    if(mode>=0 && unsigned(mode)<modes_)if(auto match=lookup(8,unsigned(mode),command,mode,false))return *match;
    if(auto match=lookup(8,modes_,command,mode,false))return *match;
    return -1;
}
}
