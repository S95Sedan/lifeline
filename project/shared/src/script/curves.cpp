#include "script/curves.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline::script {
namespace {
std::int32_t integer(float value){
    if(!std::isfinite(value)||double(value)<-2147483648.0||double(value)>=2147483648.0)throw std::runtime_error("Invalid curve integer argument");
    return std::int32_t(value);
}
unsigned address(const Invocation& c,float value,unsigned words){
    const auto bytes=integer(value+std::copysign(.5f,value));
    if(bytes<0 || bytes%4 || !c.state || !c.state->data_contains(c.program,unsigned(bytes)/4,words))throw std::runtime_error("Curve vector outside live script data");
    return unsigned(bytes)/4;
}
float read(const Invocation& c,unsigned word){
    if(!c.state)throw std::logic_error("Curve binding requires shared script data");
    const auto found=c.state->data->find(word);
    return found==c.state->data->end()?std::bit_cast<float>(c.program.word(word)):found->second;
}
}
Bindings Curves::bindings(){
    Bindings bindings;
    bindings.emplace(77,Binding{2,0,[this](const Invocation& c){
        const auto degree=integer(c.arguments[0])==0?2u:3u;
        const auto base=address(c,c.arguments[1],4*(degree+1));
        for(unsigned slot=0;slot<curves_.size();++slot)if(!curves_[slot].degree){
            Curve curve;curve.degree=degree;
            for(unsigned p=0;p<=degree;++p)for(unsigned axis=0;axis<4;++axis)curve.points[p][axis]=read(c,base+p*4+axis);
            // Quadratic constructor supplies the unused homogeneous column.
            if(degree==2)curve.points[3]={0,0,0,1};
            curves_[slot]=curve;return Reply{false,float(slot)};
        }
        return Reply{false,-1.f};
    }});
    bindings.emplace(78,Binding{3,0,[this](const Invocation& c){
        const auto slot=integer(c.arguments[0]);if(slot<0 || slot>=4)throw std::runtime_error("Curve handle outside 0..3");
        const auto t=c.arguments[1];if(!std::isfinite(t))throw std::runtime_error("Non-finite curve parameter");
        const auto output=address(c,c.arguments[2],4);const auto& curve=curves_[slot];
        if(curve.degree){
            // Original matrices 310c40/310c80 multiply [t^3,t^2,t,1]
            // or [t^2,t,1,0], then multiply the four control columns.
            constexpr float cubic[4][4]={{-1,3,-3,1},{3,-6,3,0},{-3,3,0,0},{1,0,0,0}};
            constexpr float quadratic[4][4]={{1,-2,1,0},{-2,2,0,0},{1,0,0,0},{0,0,0,0}};
            const auto& basis=curve.degree==3?cubic:quadratic;
            const float square=t*t;
            const std::array<float,4> powers=curve.degree==3?std::array<float,4>{square*t,square,t,1}:std::array<float,4>{square,t,1,0};
            std::array<float,4> weights{},value{};
            for(unsigned axis=0;axis<4;++axis)for(unsigned column=0;column<4;++column)weights[axis]+=basis[column][axis]*powers[column];
            for(unsigned axis=0;axis<4;++axis)for(unsigned column=0;column<4;++column)value[axis]+=curve.points[column][axis]*weights[column];
            if(!c.state)throw std::logic_error("Curve evaluation requires shared script data");
            for(unsigned axis=0;axis<4;++axis)(*c.state->data)[output+axis]=value[axis];
        }
        return Reply{false,1.f};
    }});
    bindings.emplace(79,Binding{1,0,[this](const Invocation& c){
        const auto slot=integer(c.arguments[0]);
        if(slot<0 || slot>=4)for(auto& curve:curves_)curve.degree=0;
        else curves_[slot].degree=0;
        return Reply{false,0.f};
    }});
    return bindings;
}
}
