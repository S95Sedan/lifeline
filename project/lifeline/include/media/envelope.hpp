#pragma once
#include <algorithm>
#include <cmath>
namespace lifeline::audio_detail {
struct Envelope {
    unsigned a,b,phase=0,wait=0;int level=0;
    void release(){if(phase<3){phase=3;wait=0;}}
    float next(){
        if(phase==4)return 0;
        if(wait){--wait;return level/32768.f;}
        unsigned shift=0,step_bits=0;bool down=false,exponential=false;
        if(phase==0){shift=(a>>10)&31;step_bits=(a>>8)&3;exponential=(a&0x8000)!=0;}
        else if(phase==1){shift=(a>>4)&15;down=exponential=true;}
        else if(phase==2){shift=(b>>8)&31;step_bits=(b>>6)&3;down=(b&0x4000)!=0;exponential=(b&0x8000)!=0;}
        else {shift=b&31;down=true;exponential=(b&32)!=0;}
        unsigned cycles=1u<<(shift>11?shift-11:0);
        int step=(down?-8+int(step_bits):7-int(step_bits))*(1<<(shift<11?11-shift:0));
        if(exponential){if(down)step=int(std::floor(double(step)*level/32768.0));else if(level>0x6000)cycles*=4;}
        wait=cycles-1;level=std::clamp(level+step,0,32767);
        if(phase==0 && level==32767){phase=1;wait=0;}
        else if(phase==1 && level<=std::min(32767,int((a&15)+1)*2048)){phase=2;wait=0;}
        else if(phase==3 && !level)phase=4;
        return level/32768.f;
    }
};
}
