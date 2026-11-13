#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace lifeline::script {
namespace {
int selector(float value){
    const double rounded=std::nearbyint(double(value));
    if(!std::isfinite(rounded)||rounded<-2147483648.||rounded>2147483647.)throw std::runtime_error("Invalid poison effect selector");
    return int(rounded);
}
math::Vector4 vector(std::span<const float> p,unsigned i,float w=0){return {p[i],p[i+1],p[i+2],w};}
}
// Original poison class: 171ba8 initializer, 171ee0 update, 172340 draw.
int Effects::create_poison(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty()&&input.size()!=21)throw std::runtime_error("Poison projectile requires 21 parameters");
    Instance effect;effect.type=3000;
    const float defaults[]{0,2,3,0,0,0,0,0,1,0,-1,0,.75f,.2f,60,1,0,65535,-1,0,123};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty()&&input[0]==0)std::copy(input.begin(),input.end(),effect.parameters.begin());
    auto& p=effect.parameters;
    for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Nonfinite poison projectile parameter");
    const auto parent=attachment_(selector(p[1]),selector(p[2]));
    effect.origin=math::transform(parent,vector(p,3,1));
    effect.direction=math::normalize_xyz(math::transform(parent,math::normalize_xyz(vector(p,6))))*p[12];
    effect.radial_axis=math::normalize_xyz(vector(p,9))*.01f;
    effect.lifetime=p[14];p[21]=p[13];
    p[22]=float(random_()&255)/255.f*6.28f-3.14f;
    const unsigned choice=(random_()&255)/128;
    effect.textures[0]=texture(3000,choice?"poisonsmoke.raw":"poisondebris.raw");
    return insert(std::move(effect));
}
bool Effects::advance_poison(Instance& effect,float frames){
    auto& p=effect.parameters;
    effect.origin=effect.origin+effect.direction;
    if(effect.origin.y<.1f)return false;
    effect.direction=effect.direction+effect.radial_axis;
    // 171f18..172048 also updates unused orientation fields, consuming one RNG.
    random_();
    p[21]+=.01f;effect.age+=frames;
    if(effect.age>=effect.lifetime)return false;
    if(!selector(p[15]))return true;
    if(!overlap_)throw std::runtime_error("Poison projectile collision service is not connected");
    // 1b4ec0 converts the mask to an unsigned word (truncate, saturate).
    const auto mask=std::uint32_t(std::clamp(double(p[17]),0.,4294967295.));
    const auto material=overlap_(effect.origin,p[21],selector(p[16]),std::bit_cast<int>(mask));
    if(!material)return true;
    const int worker=selector(p[18]);
    if(worker>=0){
        if(!post_)throw std::runtime_error("Poison projectile worker delivery is not connected");
        post_(unsigned(worker),selector(p[19]),selector(p[20]),*material);
    }
    // The hit creates 306a28, a timed smoke emitter; it has no geometry itself.
    Instance impact;impact.type=-64;impact.origin=effect.origin;
    impact.direction=math::normalize_xyz(math::transform(attachment_(2,14),{0,1,0,0}));
    insert(std::move(impact));return false;
}
bool Effects::advance_poison_impact(Instance& impact,float frames){
    // 17a0f8: blue smoke at tick 12, then white/blue smoke after tick 24.
    impact.age+=frames;
    std::array<float,32> blue{0,0,0,impact.origin.x,impact.origin.y-2,impact.origin.z,
        0,0,impact.direction.x,impact.direction.y,impact.direction.z,0,0,
        .4f,.6f,.8f,0,.4f,.6f,.8f,.6f,.4f,.6f,.8f,0,.12f,.12f,2.7f,2.7f,2.7f,70,25};
    if(impact.age>=12&&impact.age<12+frames){auto first=blue;first[3]-=1.5f;create(4,first);}
    if(impact.age<=24)return true;
    const std::array<float,32> white{0,0,0,impact.origin.x,impact.origin.y-2,impact.origin.z,
        0,0,impact.direction.x,impact.direction.y,impact.direction.z,0,0,
        1,1,1,0,1,1,1,.3f,1,1,1,0,.2f,.2f,3,4.5f,8,100,30};
    blue[3]+=3;create(4,white);create(4,blue);return false;
}
}
