#include "speech/acoustic_model.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline {
namespace {
using Size=std::uint64_t;
[[noreturn]] void invalid(){throw std::runtime_error("Invalid or unsupported original speech data");}
struct Reader {
    std::span<const unsigned char> b;
    void range(Size p,Size n)const{if(p>b.size() || n>b.size()-p)invalid();}
    std::uint16_t u16(Size p)const{range(p,2);return std::uint16_t(b[std::size_t(p)] | unsigned(b[std::size_t(p+1)])<<8);}
    std::uint32_t u32(Size p)const{return u16(p) | std::uint32_t(u16(p+2))<<16;}
    Size count(Size p)const{const auto n=u32(p);if(n>b.size())invalid();return n;}
    std::string name()const{
        range(8,128);const auto first=b.begin()+8,last=first+128;
        const auto end=std::find(first,last,0);if(end==last || end==first)invalid();
        return {first,end};
    }
    void header(std::uint32_t kind,Size minimum){
        range(0,minimum);if(u32(0)!=kind)invalid();
        const auto size=u32(4);if(size<minimum || size>b.size() || size>16*1024*1024)invalid();
        b=b.first(size); // Archive allocation padding is not model data.
    }
};
Size aligned(Size p){return (p+3)&~Size(3);}
}
OriginalSpeechData::OriginalSpeechData(std::span<const unsigned char> language,
                                     std::span<const unsigned char> context){
    Reader l{language},c{context};l.header(0x1003,0xe0);c.header(6,0xb8);
    identity_=l.name();if(c.name()!=identity_)invalid();
    // This revision uses packed stream data (flag 1). Other layouts have not
    // been recovered and must not be interpreted using this layout.
    if(l.u32(0xd8)!=1)invalid();
    const auto stream_count=l.count(0x88),state_count=l.count(0x8c);
    const auto model_state_limit=l.count(0x90)+l.count(0x9c); // 0x21f328 includes extra states.
    if(!stream_count || !state_count)invalid();
    const Size state_table=0xe0+8*l.count(0xcc); // 0x21f4d0.
    // 0x21f4c0 supplies mask/warp pairs to branch activation and selection.
    for(Size i=0;i<l.count(0xcc);++i) {
        const auto mask=l.u32(0xe0+8*i);
        const float factor=std::bit_cast<float>(l.u32(0xe4+8*i));
        if(!mask || !std::isfinite(factor) || factor<=0 || factor>4) invalid();
        warps_.push_back({mask,factor});
    }
    l.range(state_table,2*stream_count*state_count);
    for(Size state=0;state<state_count;++state){
        std::vector<std::uint16_t> row;
        for(Size stream=0;stream<stream_count;++stream)
            row.push_back(l.u16(state_table+2*(state*stream_count+stream)));
        state_streams_.push_back(std::move(row));
    }
    // 0x27b698 stores -header[0x9c]: model IDs begin AFTER the composite
    // prefix, whereas group members address raw rows directly (0x27c8f8).
    const auto group_count=l.count(0x94),composite_count=l.count(0x9c);
    if(l.count(0x90)!=state_count || model_state_limit>65536 ||
       !group_count || !composite_count || l.u32(0xac)>=state_count)invalid();
    external_acoustic_row_=std::uint16_t(l.u32(0xac));
    if(l.u32(0xb4)>=model_state_limit)invalid();
    hypothesis_reference_state_=std::uint16_t(l.u32(0xb4));
    const Size groups=state_table+2*stream_count*state_count;
    const Size composites=groups+2*(group_count+1+l.count(0x98));
    const Size biases=composites+2*(composite_count+1+l.count(0xa0));
    Size end=group_count+1;
    for(Size i=0;i<group_count;++i){
        const Size lo=l.u16(groups+2*i),hi=l.u16(groups+2*(i+1));
        if(lo!=end || hi<=lo || hi>group_count+1+l.count(0x98))invalid();
        std::vector<std::uint16_t> members;
        for(Size j=lo;j<hi;++j){const auto row=l.u16(groups+2*j);if(row>=state_count)invalid();members.push_back(row);}
        acoustic_groups_.push_back(std::move(members));end=hi;
    }
    if(end!=group_count+1+l.count(0x98))invalid();
    end=composite_count+1;
    Size bias_index=0;
    for(Size i=0;i<composite_count;++i){
        const Size lo=l.u16(composites+2*i),hi=l.u16(composites+2*(i+1));
        if(lo!=end || hi<=lo || hi>composite_count+1+l.count(0xa0))invalid();
        std::vector<SpeechCompositeTerm> terms;
        for(Size j=lo;j<hi;++j){
            const auto group=l.u16(composites+2*j);if(group>=group_count)invalid();
            terms.push_back({group,std::bit_cast<std::int16_t>(l.u16(biases+2*bias_index++))});
        }
        composite_states_.push_back(std::move(terms));end=hi;
    }
    if(bias_index!=l.count(0xa0))invalid();
    // 0x21f5a8: start of stream records, after the fixed and ushort tables.
    Size p=aligned(0xe4+2*(2*l.count(0xa0)+4*l.count(0xcc)+l.count(0x98)
                             +l.count(0x94)+stream_count*state_count+l.count(0x9c)));
    l.range(p,0);
    for(Size i=0;i<stream_count;++i){
        OriginalSpeechStream stream{};stream.source_offset=std::uint32_t(p);
        for(unsigned j=0;j<6;++j)stream.parameters[j]=std::uint32_t(l.count(p+4*j));
        const auto& h=stream.parameters;
        // 0x21f218 uses R5900 MULT with a destination and MADD. In particular
        // the packed block length is multiplied by h[3], not simply added.
        const Size size=24+4*((Size(h[0])+h[2])*h[1]+Size(h[0])*h[3]+h[3])
                             +Size(h[3])*((Size(h[5])+1)/2);
        l.range(p,size);
        stream.payload.assign(l.b.begin()+std::size_t(p+24),l.b.begin()+std::size_t(p+size));
        streams_.push_back(std::move(stream));p=aligned(p+size);
    }
    // 0x21f730 / 0x21f8c8: offsets are in halfwords relative to this table.
    const Size table=p,model_count=l.count(0xbc);
    l.range(table,(model_count+1)*4);
    Size previous=(model_count+1)*2;
    for(Size i=0;i<model_count;++i){
        const Size lo=l.u32(table+4*i),hi=l.u32(table+4*(i+1));
        if(lo<previous || hi<lo+2)invalid();
        l.range(table+2*lo,2*(hi-lo));
        OriginalSpeechModel model{l.u16(table+2*lo),l.u16(table+2*lo+2),{}};
        for(Size j=lo+2;j<hi;++j){
            const auto state=l.u16(table+2*j);if(state>=model_state_limit)invalid();
            model.states.push_back(state);
        }
        models_.push_back(std::move(model));previous=hi;
    }
    // CTX accessor family 0x21ee60..0x21ef18. Commands keep their complete
    // model sequence, including the .S records, independently of any prompt.
    const Size commands=c.count(0xa0),other=c.count(0x9c);
    const Size pool_count=c.count(0x98)+commands;
    const Size ids=aligned(0xb8+2*pool_count);
    const Size offsets=ids+4*(other+1);
    c.range(0xb8,2*pool_count);c.range(ids,4*(commands+other+2));
    // The first table partitions pronunciation paths by vocabulary word.
    // Usually there is one path per word; os_2575 has two for word 1.
    word_count_=unsigned(other);
    if(c.u32(ids)!=other+1||c.u32(ids+4*other)!=other+1+commands)invalid();
    previous=0;
    Size word=0;
    for(Size i=0;i<commands;++i){
        while(word<other&&i+other+1>=c.u32(ids+4*(word+1)))++word;
        if(word>=other||i+other+1<c.u32(ids+4*word))invalid();
        const Size lo=c.u32(offsets+4*i),hi=c.u32(offsets+4*(i+1));
        if(lo!=previous || hi<=lo || hi>pool_count)invalid();
        OriginalSpeechCommand command{std::uint32_t(other+1+i),c.u16(0xb8+2*lo),{},std::uint32_t(word)};
        for(Size j=lo+1;j<hi;++j){
            const auto model=c.u16(0xb8+2*j);if(model>=models_.size())invalid();
            command.models.push_back(model);
        }
        commands_.push_back(std::move(command));previous=hi;
    }
    if(previous!=pool_count)invalid();
    // 21ee98 supplies word flags; 21eed0 supplies the CSR successor table.
    const Size flags=offsets+4*(commands+1),graph=flags+2*(other+(other&1));
    c.range(flags,2*other);c.range(graph,4*(other+1));
    if(c.u32(graph)!=other+1)invalid();
    for(Size i=0;i<other;++i){
        word_flags_.push_back(c.u16(flags+2*i));
        const Size lo=c.u32(graph+4*i),hi=c.u32(graph+4*(i+1));
        if(hi<lo)invalid();c.range(graph+4*lo,4*(hi-lo));
        std::vector<unsigned> next;
        for(Size j=lo;j<hi;++j){const auto target=c.u32(graph+4*j);if(target>=other)invalid();next.push_back(target);}
        successors_.push_back(std::move(next));
    }
}
}
