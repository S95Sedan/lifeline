#include "game/effects/effects.hpp"
#include <algorithm>
#include <numbers>
namespace lifeline::script {
int Effects::create_orbit_light(std::span<const float> input){
    // 1a01c8: four textured planes rotating about independently chosen axes.
    if(!input.empty()&&input.size()!=11)throw std::runtime_error("Orbit light requires 11 parameters");
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=12;constexpr float defaults[]{0,0,0,0,25,0,2.5f,.52f,.92f,.71f,.25f};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty())std::copy(input.begin(),input.end(),effect.parameters.begin());
    const auto& p=effect.parameters;for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid orbit light parameter");
    if(std::nearbyint(p[0])!=0)return -1;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(unsigned i=0;i<4;++i){Particle particle;
        const float x=.5f-unit(),y=.5f-unit(),z=.5f-unit();particle.direction=math::normalize_xyz({x,y,z,0});
        particle.angle=unit()*2*std::numbers::pi_v<float>;particle.size=(unit()*.5f+.5f)*p[6];
        particle.growth=unit();particle.life=(unit()*.5f+.5f)*480;
        effect.particles.push_back(particle);
    }
    effect.textures[0]=texture(12,"pointlight3.raw");return insert(std::move(effect));
}
int Effects::create_encounter(std::span<const float> input){
    // 17d060; 32 rotating crosses of four encount_ball sprites.
    if(!input.empty()&&input.size()!=17)throw std::runtime_error("Encounter effect requires 17 parameters");
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=16;
    constexpr float defaults[]{0,0,0,0,50,0,3,20,30,.7f,1,.5f,1,60,180,60,360};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty())std::copy(input.begin(),input.end(),effect.parameters.begin());
    auto& p=effect.parameters;for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid encounter parameter");
    if(std::nearbyint(p[0])!=0)return -1;
    effect.origin=math::transform(attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2]))),{p[3],p[4],p[5],1});
    effect.lifetime=p[16];effect.textures[0]=texture(16,"encount_ball.raw");
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(unsigned i=0;i<32;++i){Particle particle;
        particle.age=unit()*p[13];particle.life=particle.age+p[14];
        particle.rotation.x=unit()*std::numbers::pi_v<float>;particle.rotation.y=unit()*std::numbers::pi_v<float>;
        particle.angular_velocity.y=(8*std::numbers::pi_v<float>+unit()*2*std::numbers::pi_v<float>)/p[14];
        particle.initial_growth=unit()*p[6];particle.far_size=p[7]+unit()*(p[8]-p[7]);
        particle.color={p[9],p[10],p[11],0};effect.particles.push_back(particle);
    }
    return insert(std::move(effect));
}
bool Effects::advance_encounter(Instance& effect,float frames){
    // 17d3f8: attachment follows animation; the final collapse emits type 11.
    const auto& p=effect.parameters;const float collapse=p[16]-p[15];
    effect.origin=math::transform(attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2]))),{p[3],p[4],p[5],1});
    if(effect.age<collapse&&effect.age+frames>=collapse){
        const std::array<float,13> light{0,0,0,effect.origin.x,effect.origin.y,effect.origin.z,p[6],p[8],.3f,.8f,.95f,1,p[15]};
        create(11,light);
    }
    effect.age+=frames;if(effect.age>effect.lifetime)return false;
    for(auto& particle:effect.particles){
        if(effect.age<particle.age){particle.color.w=0;continue;}
        float alpha=1;
        if(effect.age<particle.life){
            alpha=(effect.age-particle.age)/(particle.life-particle.age);
            particle.size=alpha*p[6]*.5f;
            particle.extent=particle.initial_growth+(1-alpha)*(particle.far_size-particle.initial_growth);
        }else if(effect.age<collapse){
            const float progress=(effect.age-particle.life)/(collapse-particle.life);
            particle.size=p[6]*.5f;particle.extent=particle.initial_growth;
            particle.angle+=(1-progress)*particle.angular_velocity.y;
        }else{
            const float progress=(effect.age-collapse)/(effect.lifetime-collapse);
            alpha=(1-progress)/(1+4*progress);particle.size=p[6]*.5f;particle.extent=particle.initial_growth;
            particle.angle+=(1-alpha)*particle.angular_velocity.y;
        }
        particle.color.w=p[12]*alpha;
    }
    return true;
}
}
