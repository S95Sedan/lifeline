#include "script/vectors.hpp"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace lifeline::script {
namespace {
using Vector=std::array<float,4>;
unsigned address(const Invocation& c,float value,unsigned count){
    // These handlers multiply a word address by four before 103c68's
    // signed half-away-from-zero byte conversion.
    const float bytes=value*4.f;
    const double rounded=std::trunc(double(bytes+std::copysign(.5f,bytes)));
    if(!std::isfinite(rounded) || rounded<0 || rounded>2147483647.0 || std::fmod(rounded,4)!=0 || !c.state)
        throw std::runtime_error("Invalid script vector address");
    const auto word=unsigned(rounded)/4;
    if(!c.state->data_contains(c.program,word,count))throw std::runtime_error("Script vector outside live data");
    return word;
}
float read(const Invocation& c,unsigned word){
    const auto found=c.state->data->find(word);
    return found==c.state->data->end()?std::bit_cast<float>(c.program.word(word)):found->second;
}
Vector read_vector(const Invocation& c,float pointer){
    const auto word=address(c,pointer,4);Vector value{};
    for(unsigned i=0;i<4;++i)value[i]=read(c,word+i);return value;
}
void write(const Invocation& c,unsigned word,const Vector& value){
    for(unsigned i=0;i<4;++i)(*c.state->data)[word+i]=value[i];
}
float dot(const Vector& a,const Vector& b){return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
}
Bindings vector_bindings(){
    Bindings result;
    result.emplace(1049,Binding{5,0,[](const Invocation& c){
        write(c,address(c,c.arguments[0],4),{c.arguments[1],c.arguments[2],c.arguments[3],c.arguments[4]});return Reply{false,0.f};
    }});
    for(unsigned opcode:{1050u,1051u})result.emplace(opcode,Binding{4,0,[opcode](const Invocation& c){
        const auto output=address(c,c.arguments[0],4);
        write(c,output,{c.arguments[1],c.arguments[2],c.arguments[3],opcode==1050?1.f:0.f});return Reply{false,0.f};
    }});
    for(unsigned opcode:{1052u,1053u,1054u,1055u,1056u})result.emplace(opcode,Binding{opcode==1053?4u:3u,0,[opcode](const Invocation& c){
        const auto output=address(c,c.arguments[0],4);
        const auto a=read_vector(c,c.arguments[1]);Vector value{};
        if(opcode==1055){for(unsigned i=0;i<4;++i)value[i]=a[i]*c.arguments[2];}
        else {
            const auto b=read_vector(c,c.arguments[2]);
            if(opcode==1056)value={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0],0};
            else for(unsigned i=0;i<4;++i)value[i]=opcode==1052?a[i]+b[i]:opcode==1054?a[i]-b[i]:b[i]*c.arguments[3]+a[i];
        }
        // Snapshot operands before writing: destination may overlap either input.
        write(c,output,value);return Reply{false,0.f};
    }});
    result.emplace(1057,Binding{2,0,[](const Invocation& c){return Reply{false,dot(read_vector(c,c.arguments[0]),read_vector(c,c.arguments[1]))};}});
    result.emplace(1078,Binding{2,0,[](const Invocation& c){
        // 113b70 -> 1ba678 -> 1bb660: acos of XYZ dot product, no normalization.
        const auto cosine=dot(read_vector(c,c.arguments[0]),read_vector(c,c.arguments[1]));
        // Independently normalized parallel vectors can round a few ULPs
        // outside the acos domain. Preserve the endpoint angle, not a NaN
        // subsequently written into the object's rotation (Boss Box aim).
        return Reply{false,std::acos(std::clamp(cosine,-1.f,1.f))};
    }});
    result.emplace(1058,Binding{2,0,[](const Invocation& c){
        const auto output=address(c,c.arguments[0],4);auto value=read_vector(c,c.arguments[1]);
        const float length=std::sqrt(dot(value,value));
        // Original zero-length divide saturates Q, leaving zero XYZ and W=0.
        if(length==0)value={0,0,0,0};
        else {const float reciprocal=1.f/length;for(unsigned i=0;i<3;++i)value[i]*=reciprocal;value[3]=0;}
        write(c,output,value);return Reply{false,length};
    }});
    result.emplace(1059,Binding{1,0,[](const Invocation& c){const auto value=read_vector(c,c.arguments[0]);return Reply{false,std::sqrt(dot(value,value))};}});
    result.emplace(1068,Binding{2,0,[](const Invocation& c){
        // 112b38 -> 1cb718: Euclidean XYZ distance, without modifying inputs.
        auto a=read_vector(c,c.arguments[0]);const auto b=read_vector(c,c.arguments[1]);
        for(unsigned i=0;i<3;++i)a[i]-=b[i];return Reply{false,std::sqrt(dot(a,a))};
    }});
    for(unsigned opcode:{1076u,1077u})result.emplace(opcode,Binding{2,0,[opcode](const Invocation& c){
        const auto output=address(c,c.arguments[0],4);auto value=read_vector(c,c.arguments[1]);
        // 1d7d60 / 1d7e00 map the authored curved coordinate system to/from
        // its unwrapped surface. X and W are carried through unchanged.
        constexpr float radius=729.5f;float angle;
        if(opcode==1076){
            angle=value[2]/radius;const float distance=radius-value[1];
            value[2]=distance*std::sin(angle);value[1]=radius-distance*std::cos(angle);
        }else {
            const float distance=radius-value[1];angle=std::atan2(value[2],distance);
            value[1]=radius-std::sqrt(value[2]*value[2]+distance*distance);value[2]=angle*radius;
        }
        write(c,output,value);return Reply{false,angle};
    }});
    for(unsigned axis=0;axis<4;++axis){
        result.emplace(1060+axis,Binding{1,0,[axis](const Invocation& c){return Reply{false,read(c,address(c,c.arguments[0],axis+1)+axis)};}});
        result.emplace(1064+axis,Binding{2,0,[axis](const Invocation& c){
            const auto word=address(c,c.arguments[0],axis+1)+axis;
            (*c.state->data)[word]=c.arguments[1];return Reply{false,c.arguments[1]};
        }});
    }
    return result;
}
}
