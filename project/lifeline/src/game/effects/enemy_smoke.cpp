#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>

namespace lifeline::script {
int Effects::create_dust(std::span<const float> input){
    // 193cf8..194214 emits five independent smoke2 children; its own
    // constructor returns zero, so the temporary parent is released.
    if(!input.empty()&&input.size()!=5)throw std::runtime_error("Dust emitter requires five parameters");
    Instance parent;parent.type=4000;const int reserved=insert(std::move(parent));if(reserved<0)return -1;
    const auto bitmap=texture(4000,"smoke2.raw");
    try{
        for(unsigned direction=0;direction<5;++direction){
            std::array<float,32> p{};
            if(!input.empty())std::copy(input.begin(),input.end(),p.begin()+1);
            if(direction==0)p[9]=1;
            else if(direction<3)p[8]=direction==1?-1.f:1.f;
            else p[10]=direction==3?-1.f:1.f;
            for(unsigned i:{13u,14u,15u,17u,18u,19u,21u,22u,23u})p[i]=.7f;
            p[20]=.5f;p[25]=p[26]=std::bit_cast<float>(direction==0?0x3cc49ba6u:0x3d449ba6u);
            p[27]=.7f;p[28]=std::bit_cast<float>(0x3fb7775cu);p[29]=5;p[30]=60;p[31]=20;
            create_smoke(4,p,bitmap);
        }
    }catch(...){erase(reserved);throw;}
    erase(reserved);return -1;
}
int Effects::create_omen(std::span<const float> input){
    // 192820 stores an attachment, local position and scale. 1928f8 emits
    // a leeomen child after strictly more than eight simulation frames.
    if(!input.empty()&&input.size()!=6)throw std::runtime_error("Omen emitter requires six parameters");
    Instance emitter;emitter.type=4003;emitter.parameters[5]=1;
    if(!input.empty())std::copy(input.begin(),input.end(),emitter.parameters.begin());
    for(float v:emitter.parameters)if(!std::isfinite(v))throw std::runtime_error("Invalid omen parameter");
    emitter.textures[0]=texture(4003,"leeomen.raw");
    return insert(std::move(emitter));
}
void Effects::advance_omen(Instance& emitter,float frames){
    emitter.age+=frames;if(emitter.age<=8)return;emitter.age=0;
    std::array<float,32> p{};const auto& source=emitter.parameters;
    std::copy_n(source.begin(),5,p.begin()+1);p[9]=1;
    for(unsigned i:{12u,13u,14u,15u,17u,18u,19u,21u,22u,23u})p[i]=1;
    p[20]=.18f;p[25]=p[26]=source[5]*std::bit_cast<float>(0x3d638e39u);
    p[27]=source[5]*3;p[28]=source[5]*std::bit_cast<float>(0x40555554u);p[29]=source[5]*4;
    p[30]=40;p[31]=15;
    create_smoke(-65,p,emitter.textures[0]);
}
}
