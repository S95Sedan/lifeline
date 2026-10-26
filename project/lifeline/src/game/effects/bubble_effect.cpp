#include "game/effects/effects.hpp"
#include <algorithm>
namespace lifeline::script {
int Effects::create_bubbles(std::span<const float> input){
    // 16fa48: fixed pool of 32 bubbles, all initially inactive.
    if(!input.empty()&&input.size()!=13)throw std::runtime_error("Bubble emitter requires 13 parameters");
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=10004;constexpr float defaults[]{0,0,0,0,0,0,1,1,1,.5f,20,.05f,.25f};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty()&&std::nearbyint(input[0])==0)std::copy(input.begin(),input.end(),effect.parameters.begin());
    for(float v:effect.parameters)if(!std::isfinite(v))throw std::runtime_error("Invalid bubble parameter");
    effect.particles.resize(32);effect.textures[0]=texture(10004,"bubble.raw");return insert(std::move(effect));
}
void Effects::advance_bubbles(Instance& effect){
    // 16fba8 is intentionally per-update, independent of the dt argument.
    const auto& p=effect.parameters;bool attempted=false;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(auto& bubble:effect.particles){
        if(bubble.growth!=0){
            bubble.position.y+=bubble.size*.5f;bubble.angle+=.1f;
            const auto hit=surface_contact_?surface_contact_(bubble.position,bubble.size,-1,-1):std::nullopt;
            if(hit){bubble.position=hit->point+hit->normal*bubble.size;continue;}
            if(bubble.position.y>p[10]){bubble.position={};bubble.size=0;bubble.angle=0;bubble.growth=0;}
        }else if(!attempted){
            attempted=true;if(unit()>=.1f)continue;
            bubble.position=math::transform(attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2]))),{p[3],p[4],p[5],1});
            bubble.size=p[11]+(p[12]-p[11])*unit();bubble.angle=0;bubble.growth=1;
        }
    }
}
}
