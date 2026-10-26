#include "script/memory.hpp"
#include <bit>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <array>

namespace lifeline::script {
namespace {
unsigned offset(float v){const double rounded=std::round(double(v));if(!std::isfinite(v) || rounded<0 || rounded>UINT32_MAX)throw std::runtime_error("Invalid script byte address");return unsigned(rounded);}
int integer(float v){if(!std::isfinite(v) || double(v)<INT32_MIN || double(v)>INT32_MAX)throw std::runtime_error("Invalid script memory argument");return int(std::nearbyint(v));}
class Memory {
    const Invocation& call_;
public:
    explicit Memory(const Invocation& c):call_(c){if(!c.state)throw std::runtime_error("Script memory requires mutable state");}
    void range(unsigned address,unsigned count)const{
        if(!count)return;
        const std::uint64_t end=std::uint64_t(address)+count;
        if(end>UINT32_MAX || !call_.state->data_contains(call_.program,address/4,unsigned((end+3)/4-address/4)))throw std::runtime_error("Script memory span exceeds live allocation");
    }
    unsigned word(unsigned index)const{const auto it=call_.state->data->find(index);return it==call_.state->data->end()?(index<call_.program.size()?call_.program.word(index):0):std::bit_cast<unsigned>(it->second);}
    unsigned read(unsigned address)const{range(address,1);return (word(address/4)>>(8*(address%4)))&255;}
    void write(unsigned address,unsigned value){range(address,1);const unsigned shift=(address%4)*8;(*call_.state->data)[address/4]=std::bit_cast<float>((word(address/4)&~(255u<<shift))|((value&255)<<shift));}
    std::string string(unsigned address)const{std::string text;for(;;){const auto byte=read(address++);if(!byte)return text;text+=char(byte);}}
    void string(unsigned address,const std::string& text){if(text.size()>=UINT32_MAX)throw std::runtime_error("Script string is too large");range(address,unsigned(text.size()+1));for(unsigned i=0;i<text.size();++i)write(address+i,static_cast<unsigned char>(text[i]));write(address+unsigned(text.size()),0);}
};
}
void write_script_string(const Invocation& c,float address,const std::string& text){Memory(c).string(offset(address),text);}
std::string read_script_string(const Invocation& c,float address){return Memory(c).string(offset(address));}
Bindings memory_bindings(){
    Bindings result;
    // 142260 calls strlen on the word-addressed script string.
    result.emplace(685,Binding{1,0,[](const Invocation& c){return Reply{false,float(Memory(c).string(offset(float(integer(c.arguments[0]))*4)).size())};}});
    for(unsigned opcode:{1087u,1088u})result.emplace(opcode,Binding{opcode==1087?4u:5u,0,[opcode](const Invocation& c){
        Memory memory(c);const auto base=offset(c.arguments[0]*4.f);
        // 114160/114294: PS2 MULT s0,s0,s2 writes its low word to s0.
        const auto row=std::uint32_t(integer(c.arguments[1]));
        const auto stride=std::uint32_t(integer(c.arguments[2]));
        const auto column=std::uint32_t(integer(c.arguments[3]));
        const auto index=std::bit_cast<std::int32_t>(row*stride+column);
        const auto location=std::int64_t(base)+std::int64_t(index)*4;
        if(location<0||location>UINT32_MAX||location%4)throw std::runtime_error("Invalid matrix element address");
        const auto address=unsigned(location);memory.range(address,4);
        if(opcode==1087)return Reply{false,std::bit_cast<float>(memory.word(address/4))};
        const auto bits=std::bit_cast<unsigned>(c.arguments[4]);
        for(unsigned i=0;i<4;++i)memory.write(address+i,bits>>(8*i));return Reply{false,0.f};
    }});
    result.emplace(1090,Binding{3,0,[](const Invocation& c){
        // 113fc0: word pointer, signed element index, numeric integer value.
        // Store integer bits, not the float representation of that integer.
        Memory memory(c);const auto base=offset(c.arguments[0]*4.f);
        const auto destination=std::int64_t(base)+std::int64_t(integer(c.arguments[1]))*4;
        if(destination<0 || destination>UINT32_MAX || destination%4)throw std::runtime_error("Invalid indexed script word address");
        const auto address=unsigned(destination),value=unsigned(integer(c.arguments[2]));memory.range(address,4);
        for(unsigned i=0;i<4;++i)memory.write(address+i,value>>(8*i));return Reply{false,0.f};
    }});
    result.emplace(687,Binding{3,0,[](const Invocation& c){Memory memory(c);const auto address=offset(c.arguments[0]);const int value=integer(c.arguments[1]),count=integer(c.arguments[2]);if(count<0)throw std::runtime_error("Negative script fill size");memory.range(address,unsigned(count));for(unsigned i=0;i<unsigned(count);++i)memory.write(address+i,unsigned(value));return Reply{false,0.f};}});
    result.emplace(686,Binding{3,0,[](const Invocation& c){Memory memory(c);auto destination=offset(c.arguments[0]),source=offset(c.arguments[1]);const int size=integer(c.arguments[2]);if(size<0)throw std::runtime_error("Negative script copy size");unsigned count=unsigned(size);memory.range(destination,count);memory.range(source,count);
        // 298e9c copies forward: aligned spans use 16-byte loads/stores,
        // then eight-byte groups and a byte tail. Preserve overlap behavior.
        auto copy=[&](unsigned count){std::array<unsigned,16> bytes{};for(unsigned i=0;i<count;++i)bytes[i]=memory.read(source+i);for(unsigned i=0;i<count;++i)memory.write(destination+i,bytes[i]);source+=count;destination+=count;};
        if(count>=32 && ((destination|source)&15)==0){while(count>=32){copy(16);copy(16);count-=32;}while(count>=8){copy(8);count-=8;}}
        while(count--){copy(1);}return Reply{false,0.f};
    }});
    result.emplace(688,Binding{2,0,[](const Invocation& c){Memory memory(c);return Reply{false,memory.string(offset(c.arguments[0]))==memory.string(offset(c.arguments[1]))?1.f:0.f};}});
    result.emplace(692,Binding{2,0,[](const Invocation& c){Memory memory(c);const auto destination=offset(c.arguments[0]);auto text=memory.string(destination);text+=memory.string(offset(c.arguments[1]));memory.string(destination,text);return Reply{false,1.f};}});
    result.emplace(689,Binding{3,0,[](const Invocation& c){Memory memory(c);const auto destination=offset(c.arguments[0]);const int format=integer(c.arguments[1]);if(format<0 || format>7)return Reply{false,0.f};
        std::ostringstream output;output.imbue(std::locale::classic());
        if(format==2){if(!std::isfinite(c.arguments[2]))throw std::runtime_error("Invalid formatted script float");output<<std::fixed<<std::setprecision(2)<<std::setw(6)<<c.arguments[2];}
        else if(format==3)output<<memory.string(offset(float(integer(c.arguments[2]))*4));
        else {const int value=integer(c.arguments[2]);if(format==1)output<<std::hex<<unsigned(value);else if(format>=4){const unsigned width=format==4?2:format==5?4:format==6?6:3;output<<std::setfill('0')<<std::internal<<std::setw(width)<<value;}else output<<value;}
        memory.string(destination,output.str());return Reply{false,0.f};
    }});
    return result;
}
}
