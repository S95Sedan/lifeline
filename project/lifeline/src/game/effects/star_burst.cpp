#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <numbers>
namespace lifeline::script {
int Effects::create_star_burst(std::span<const float> input){
    // 16e830 uses only attachment and local XYZ from the payload. Its
    // particle speeds, durations and colors are authored constructor constants.
    if(!input.empty()&&input.size()!=6)throw std::runtime_error("Star burst requires six parameters");
    if(count()==slots_.size())return -1;
    const std::array<float,6> defaults{0,2,1,0,0,0};const auto p=input.empty()?std::span<const float>(defaults):input;
    for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid star burst parameter");
    Instance effect;effect.type=10012;effect.origin=math::transform(attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2]))),{p[3],p[4],p[5],1});
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(unsigned i=0;i<40;++i){Particle particle;particle.position=effect.origin;
        const float x=unit()*2-1,y=unit()*2-1,z=unit()*2-1;particle.direction=math::normalize_xyz({x,y,z,0});
        if(i<32){particle.direction=particle.direction*(.24f+.24f*unit());particle.growth=.4f+.4f*unit();}
        else{particle.angle=unit()*2*std::numbers::pi_v<float>-std::numbers::pi_v<float>;particle.size=4;particle.growth=.8f+.8f*unit();}
        particle.initial_growth=particle.growth;particle.life=60+60*unit();effect.particles.push_back(particle);
    }
    effect.particles[32].life=120;effect.particles[33].life=120;effect.particles[33].growth*=1.2f;effect.particles[33].initial_growth=effect.particles[33].growth;
    effect.particles[33].size=0;effect.particles[33].direction={1,0,0,0};effect.particles[33].angle=-std::numbers::pi_v<float>/2;
    effect.textures[0]=texture(10012,"star_flare05.raw");effect.textures[1]=texture(10012,"ring04.raw");return insert(std::move(effect));
}
bool Effects::advance_star_burst(Instance& effect,float frames){
    unsigned alive=0;for(unsigned i=0;i<40;++i){auto& particle=effect.particles[i];
        if(i<32){particle.position=particle.position+particle.direction*frames;particle.direction=particle.direction*(frames*.95f);particle.direction.y-=frames*std::bit_cast<float>(0x3ac49ba6u);}
        particle.size+=frames*particle.growth;particle.growth*=frames*.95f;
        if(particle.age<particle.life){particle.age+=frames;++alive;}
    }
    return alive!=0;
}
}
