#include "media/room_reverb.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace lifeline {
namespace {
// Filter/delay constants: PS2 libsd room presets (ps2dev/ps2sdk,
// iop/sound/libsd/src/effect.c). Numerical signal-processing parameters.
constexpr unsigned sizes[]{1,0x26c,0x1f4,0x484,0x6fe,0xade,0xf6c,0x1804,0x1804,0x3c0};
constexpr std::uint32_t presets[10][16]={
{0,0,0,0,0,0x00010001,0,0,0,0x00010001,0,0,0,0,0,0},
{0x005B007D,0x54B86D80,0x0000BED0,0xBA800000,0x53005800,0x033304D6,0x022703F0,0x01EF0374,0x01B70336,0x01B60335,0x01B50334,0x01B50334,0x01B50334,0x013601B4,0x005C00B8,0x80008000},
{0x00250033,0x4FA870F0,0x4410BCE0,0x9C00C0F0,0x4EC05280,0x031B03E4,0x02AF03A4,0x02660372,0x025D031C,0x018E025C,0x0135022F,0x00B701D2,0x00B5018F,0x008000B4,0x0026004C,0x80008000},
{0x007F00B1,0x4FA870F0,0x4510BCE0,0xB4C0BEF0,0x4EC05280,0x076B0904,0x065F0824,0x061607A2,0x05ED076C,0x042E05EC,0x0305050F,0x02B70462,0x0265042F,0x01B20264,0x00800100,0x80008000},
{0x00A900E3,0x4FA86F60,0x4510BCE0,0xA680BEF0,0x52C05680,0x0B580DFB,0x0A3C0D09,0x09730BD9,0x08DA0B59,0x05E908D9,0x04B007EC,0x03D206EF,0x031D05EA,0x0238031C,0x00AA0154,0x80008000},
{0x013901A5,0x50006000,0xB8004C00,0xC000BC00,0x5C006000,0x11BB15BA,0x10BD14C2,0x0DC111BC,0x0DC311C0,0x09C10DC0,0x07C10BC4,0x06CD0A00,0x05C109C2,0x041A05C0,0x013A0274,0x80008000},
{0x0231033D,0x50007E00,0xB000B400,0xB0004C00,0x54006000,0x1A311ED6,0x183B1D14,0x16B21BC2,0x15EF1A32,0x105515EE,0x0F2D1334,0x0C5D11F6,0x0AE11056,0x07A20AE0,0x02320464,0x80008000},
{0x00030003,0x7FFF7FFF,0,0x81000000,0,0x0FFD1FFD,0x00091009,0,0x00091009,0x1FFF1FFF,0x1FFE1FFE,0x1FFE1FFE,0x1FFE1FFE,0x10041008,0x00040008,0x80008000},
{0x00030003,0x7FFF7FFF,0,0,0,0x0FFD1FFD,0x00091009,0,0x00091009,0x1FFF1FFF,0x1FFE1FFE,0x1FFE1FFE,0x1FFE1FFE,0x10041008,0x00040008,0x80008000},
{0x00130017,0x4FA870F0,0x4510BCE0,0x8500BEF0,0x54C05F80,0x02AF0371,0x01DF02E5,0x01D702B0,0x026A0358,0x011E01D6,0x00B1012D,0x0059011F,0x00E301A0,0x00400058,0x00140028,0x80008000}};
constexpr int filter[]{-1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,10246,16384,10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1};
float bounded(float value){return std::clamp(value,-1.f,32767.f/32768);}
}
void RoomReverb::clear(){std::fill(delay_.begin(),delay_.end(),0.f);input_={};output_={};cursor_=phase_=history_=pending_=0;energy_=0;}
void RoomReverb::preset(unsigned mode){
    if(mode>=std::size(sizes))throw std::runtime_error("Invalid room reverb preset");
    for(unsigned i=0;i<32;++i)parameters_[i]=static_cast<unsigned short>(presets[mode][i/2]>>((i&1)*16));
    delay_.assign(mode?sizes[mode]*8:0,0);clear();
}
void RoomReverb::depth(float value){if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Invalid room reverb depth");depth_=value;}
std::array<float,2> RoomReverb::tick(float left,float right){
    const auto coefficient=[&](unsigned i){const int value=parameters_[i];return (value<32768?value:value-65536)/32768.f;};
    const auto at=[&](int offset){const int length=int(delay_.size());int index=(int(cursor_)+offset)%length;return unsigned(index<0?index+length:index);};
    const auto read=[&](int offset){return delay_[at(offset)];};
    const auto write=[&](int offset,float value){auto& stored=delay_[at(offset)];const float next=bounded(value);energy_+=double(next)*next-double(stored)*stored;stored=next;};
    const auto address=[&](unsigned i){return int(parameters_[i])*4;};
    const float inputs[]{left*coefficient(30),right*coefficient(31)};
    for(unsigned side=0;side<2;++side){
        const auto same=address(10+side),cross=address(18+side);
        const auto a=read(same-1),b=read(cross-1);
        write(same,(inputs[side]+read(address(16+side))*coefficient(7)-a)*coefficient(2)+a);
        write(cross,(inputs[side]+read(address(24+(1-side)))*coefficient(7)-b)*coefficient(2)+b);
    }
    std::array<float,2> result{};
    for(unsigned side=0;side<2;++side){
        float value=0;for(unsigned i=0;i<4;++i){constexpr unsigned taps[]{12,14,20,22};value+=coefficient(3+i)*read(address(taps[i]+side));}
        for(unsigned stage=0;stage<2;++stage){const int destination=address(26+stage*2+side),source=destination-address(stage);const float echo=read(source),gain=coefficient(8+stage);value-=gain*echo;write(destination,value);value=value*gain+echo;}
        result[side]=bounded(value);
    }
    cursor_=(cursor_+1)%unsigned(delay_.size());return result;
}
std::array<float,2> RoomReverb::process(float left,float right){
    if(delay_.empty())return {left,right};
    if(left!=0||right!=0)pending_=80;else if(pending_)--pending_;
    input_[history_]={left,right};std::array<float,2> wet{};
    if(phase_==0){
        for(unsigned i=0;i<39;++i){const auto& sample=input_[(history_+39-i)%39];for(unsigned side=0;side<2;++side)wet[side]+=sample[side]*(filter[i]/32768.f);}
        wet=tick(wet[0],wet[1]);
    }
    output_[history_]=wet;wet={};
    for(unsigned i=0;i<39;++i){const auto& sample=output_[(history_+39-i)%39];for(unsigned side=0;side<2;++side)wet[side]+=sample[side]*(filter[i]/16384.f);}
    history_=(history_+1)%39;phase_^=1;
    return {bounded(left+wet[0]*depth_),bounded(right+wet[1]*depth_)};
}
void RoomReverb::process(std::span<float> samples){
    if(samples.size()%2)throw std::runtime_error("Reverb requires stereo samples");
    for(std::size_t i=0;i<samples.size();i+=2){const auto value=process(samples[i],samples[i+1]);samples[i]=value[0];samples[i+1]=value[1];}
}
}
