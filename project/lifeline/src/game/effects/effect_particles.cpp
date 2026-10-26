#include "game/effects/effects.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lifeline::script {
namespace {
constexpr float pi=std::numbers::pi_v<float>;
math::Vector4 xyz(std::span<const float> p,unsigned i,float w=0){return {p[i],p[i+1],p[i+2],w};}
}
int Effects::insert(Instance effect){
    const auto free=std::find_if(slots_.begin(),slots_.end(),[](const auto& value){return !value;});
    if(free==slots_.end())return -1;
    *free=std::move(effect);return int(free-slots_.begin());
}
int Effects::create_particles(int type,std::span<const float> input){
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=type;
    const unsigned parameter_count=type==10007?24:type==1002?19:type==1005?13:12;
    if(!input.empty()&&input.size()!=parameter_count&&!(type==4002&&input.size()==6&&input[0]==1))throw std::runtime_error("Particle effect parameter count differs");
    if(type==1002){const float values[]{0,2,3,0,0,1,0,1,1,1,1,1,1,2,1,1,1,10,8};std::copy(std::begin(values),std::end(values),effect.parameters.begin());}
    else if(type==1005){const float values[]{0,0,0,0,0,0,1,0,1,1,1,1,30};std::copy(std::begin(values),std::end(values),effect.parameters.begin());}
    else{const float values[]{0,2,1,0,0,0,-1,65535,-1,-1,0,0};std::copy(std::begin(values),std::end(values),effect.parameters.begin());}
    if(!input.empty()){
        for(float value:input)if(!std::isfinite(value))throw std::runtime_error("Non-finite particle parameter");
        // 194744: mode 1 overrides only attachment and origin; mode 0 uses
        // the complete caller payload. Impact 18cc40 has no mode prefix.
        if(type==4002&&input[0]==1)std::copy(input.begin()+1,input.begin()+6,effect.parameters.begin()+1);
        else std::copy(input.begin(),input.end(),effect.parameters.begin());
    }
    const auto& p=effect.parameters;
    if(type==10007){
        if(input.empty())throw std::runtime_error("Aura default parameters not installed");
        effect.lifetime=p[23];
        // 185a78: the draw path uses the first phase, but initialization
        // consumes the complete eight-by-three phase/speed table's RNG.
        for(unsigned ray=0;ray<8;++ray){for(unsigned axis=0;axis<3;++axis){
            if(!(ray&1)){const auto angle=random_()<<4;if(!ray&&!axis)effect.aura_angle=angle;}
            const auto step=(random_()+65536u)>>8;if(!ray&&!axis)effect.aura_step=step;
        }random_();}
        return insert(std::move(effect));
    }
    const auto selector=[](float v){if(v<-2147483648.f||double(v)>=2147483648.)throw std::runtime_error("Invalid particle attachment");return int(std::nearbyint(v));};
    auto matrix=attachment_(selector(p[type==1005?0:1]),selector(p[type==1005?1:2]));
    effect.origin=math::transform(matrix,xyz(p,type==1005?2:3,1));
    effect.direction=math::transform(matrix,xyz(p,5));effect.direction.x=0;
    effect.lifetime=type==1005?p[12]:120;
    // Reserve the parent before its children, matching the shared slot table.
    const int result=insert(std::move(effect));if(result<0)return -1;
    auto& parent=*slots_[result];const auto origin=parent.origin;
    const auto random=[&]{return float(random_()&4095)/4095.f;};
    if(type==1002){
        const auto& values=parent.parameters;
        parent.direction=math::normalize_xyz(math::transform(matrix,xyz(values,6)));
        Particle spray;spray.kind=Particle::spray;spray.position=origin;spray.direction=parent.direction;spray.life=values[18];
        spray.growth=values[13];spray.size=values[14];spray.far_size=values[15];spray.color={values[9],values[10],values[11],values[12]};
        spray.texture=texture(1002,"bloodspray.raw");parent.particles.push_back(spray);
        // 186f98..1872c8 creates eight independent blood droplets.
        for(unsigned i=0;i<8;++i){
            if(count()==slots_.size())break;
            Instance drop;drop.type=-4;Particle particle;particle.kind=Particle::smoke;particle.position=origin;
            const float brightness=.5f+random(),x=random()-.5f,y=random()-.5f,z=random()-.5f;
            particle.direction=parent.direction+math::Vector4{x,y,z,0};particle.color=spray.color;particle.color.x*=brightness;particle.color.y*=brightness;particle.color.z*=brightness;
            particle.speed=random()*.18f*values[13];particle.size=values[16]+random()*.1f;particle.growth=values[17]+random()*.1f;particle.life=40;
            particle.angle=float(random_()&255)/255*6.28f-3.14f;particle.texture=texture(1002,"bloodsmoke.raw");drop.particles.push_back(particle);insert(std::move(drop));
        }
    }else if(type==1005){
        Instance fragments;fragments.type=-1;
        // 19ade8: sixteen rays, full cone, speed .5.. .6, life 15..30.
        for(unsigned i=0;i<16;++i){
            const float azimuth=random()*2*pi,polar=(random()*2-1)*pi;
            Particle particle;particle.kind=Particle::fragment;particle.position=origin;
            particle.direction={std::sin(polar)*std::cos(azimuth),std::sin(polar)*std::sin(azimuth),std::cos(polar),0};
            particle.speed=.5f+.1f*random();particle.life=15+15*random();particle.size=.05f;particle.growth=3;particle.color={1,.6f,0,1};
            fragments.particles.push_back(particle);
        }
        insert(std::move(fragments));
        for(unsigned i=0;i<2;++i){
            Instance burst;burst.type=-2;Particle particle;particle.kind=Particle::flash;particle.position=origin;particle.direction=parent.direction;
            particle.life=10;particle.size=.25f;particle.color={1,.6f,0,1};particle.angle=float(random_()&255)/255*6.28f-3.14f;
            particle.texture=texture(1005,"smoke2.raw");burst.particles.push_back(particle);insert(std::move(burst));
        }
    }else{
        // 194698: 32 stars and 8 expanding rings. Each consumes six random
        // values, including the rings' independent initial rotation.
        for(unsigned i=0;i<40;++i){
            Particle particle;particle.position=origin;particle.kind=i<32?Particle::star:Particle::ring;
            const float x=random()*2-1,y=random()*2-1,z=random()*2-1;
            particle.direction=math::normalize_xyz({x,y,z,0});
            if(i<32){particle.speed=.24f+random()*.24f;particle.growth=.06f+random()*.06f;}
            else{particle.angle=random()*2*pi-pi;particle.growth=.24f+random()*.24f;particle.size=4;}
            particle.life=60+random()*60;particle.color={.5f,.7f,1,1};particle.initial_growth=particle.growth;
            if(i==32)particle.life=120;
            if(i==33){particle.life=120;particle.size=0;particle.angle=0;particle.growth*=1.5f;particle.initial_growth=particle.growth;}
            particle.texture=texture(4002,i<32?"lee_star.raw":"lee_ring.raw");parent.particles.push_back(particle);
        }
    }
    return result;
}
bool Effects::advance_particles(Instance& effect,float dt){
    effect.age+=dt;
    if(effect.type==10007){effect.aura_angle+=effect.aura_step;return effect.age<=effect.lifetime;}
    if(effect.type==1005){
        if(effect.age>effect.lifetime)return false;
        // 18d3c4..18d508 consumes three random values on every live update,
        // even when this tick does not emit a smoke child.
        const auto random=[&]{return float(random_()&4095)/4095.f;};
        const float x=random()*.06f-.03f,z=random()*.06f-.03f,life=60+random()*30;
        if(effect.age<=1.5f||(effect.age>=effect.lifetime*.5f&&effect.age<effect.lifetime*.5f+dt)){
            Instance cloud;cloud.type=-3;Particle particle;particle.kind=Particle::smoke;particle.position=effect.origin;
            particle.direction=effect.direction+math::Vector4{x,0,z,0};particle.speed=.04f;particle.life=life;particle.size=.5f;particle.growth=4.5f;
            particle.color={effect.parameters[8],effect.parameters[9],effect.parameters[10],effect.parameters[11]};
            particle.texture=texture(1005,"smoke1.raw");particle.angle=float(random_()&255)/255*6.28f-3.14f;
            cloud.particles.push_back(particle);insert(std::move(cloud));
        }
        return true;
    }
    bool alive=false;
    for(auto& particle:effect.particles){
        if(particle.age>particle.life)continue;
        alive=true;particle.age+=dt;
        if(particle.kind==Particle::fragment){
            auto displacement=particle.direction*(particle.speed*dt);particle.position=particle.position+displacement;
            displacement.y-=.01f;particle.speed=std::sqrt(math::dot_xyz(displacement,displacement));
        }else if(particle.kind==Particle::spray){particle.extent+=particle.growth;particle.size+=particle.growth*.2f;particle.growth*=.95f;}
        else if(particle.kind==Particle::star){
            particle.position=particle.position+particle.direction*(particle.speed*dt);
            auto velocity=particle.direction*(particle.speed*dt*.95f);velocity.y-=.0015f*dt;
            particle.speed=std::sqrt(math::dot_xyz(velocity,velocity));particle.direction=math::normalize_xyz(velocity);
            particle.size+=particle.growth*dt;particle.growth*=.95f*dt;
        }else if(particle.kind==Particle::ring){particle.size+=particle.growth*dt;particle.growth*=.95f*dt;}
        else if(particle.kind==Particle::smoke){particle.position=particle.position+particle.direction*(particle.speed*dt);particle.angle+=dt/particle.life*(effect.type==-4?.01f:pi*.5f);}
        else if(particle.kind==Particle::flash){
            const float t=std::min(particle.age/5.f,2.f);particle.size=.25f+t*.25f;
            particle.color=t<=1?math::Vector4{1,.6f-.2f*t,0,1}:math::Vector4{1-.5f*(t-1),.4f-.3f*(t-1),0,2-t};
        }
    }
    return alive;
}
}
