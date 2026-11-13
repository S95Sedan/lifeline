#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <numbers>

namespace lifeline::script {
int Effects::create_energy_projectile(std::span<const float> input){
    if(!input.empty()&&input.size()!=18)throw std::runtime_error("Energy projectile requires 18 parameters");
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=10002;
    constexpr float defaults[]{0,2,3,0,0,0,0,0,1,1,8,10,1,0,65535,-1,0,123};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty()&&std::nearbyint(input[0])==0)std::copy(input.begin(),input.end(),effect.parameters.begin());
    const auto& p=effect.parameters;for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid energy projectile parameter");
    const auto basis=attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2])));
    effect.origin=math::transform(basis,{p[3],p[4],p[5],1});effect.direction=math::normalize_xyz(math::transform(basis,{p[6],p[7],p[8],0}));
    effect.trail[0]=math::normalize_xyz(math::cross_xyz(effect.direction,{0,1,0,0}));effect.trail[1]=math::cross_xyz(effect.trail[0],effect.direction);
    for(unsigned i=0;i<12;++i){Particle point;point.angle=float(1024+(random_()&1023))*(2*std::numbers::pi_v<float>/65536);effect.particles.push_back(point);}
    effect.lifetime=p[11];return insert(std::move(effect));
}
bool Effects::advance_energy_projectile(Instance& effect,float frames){
    auto& p=effect.parameters;effect.aura_angle=(effect.aura_angle+random_())&65535;
    effect.origin=effect.origin+effect.direction*(p[10]*.125f);p[9]+=.05f;effect.age+=frames;
    if(effect.age>effect.lifetime)return false;
    if(std::nearbyint(p[12])==0)return true;
    if(!capsule_contact_)throw std::runtime_error("Energy projectile collision service is not connected");
    const auto mask=std::uint32_t(std::clamp(double(p[14]),0.,4294967295.));
    const auto hit=capsule_contact_(effect.origin+effect.direction*(p[10]*.5f),effect.origin+effect.direction*(-p[10]*.5f),p[9]*.5f,int(std::nearbyint(p[13])),std::bit_cast<int>(mask));
    if(!hit)return true;
    if(p[15]>=0){
        if(!post_)throw std::runtime_error("Energy projectile worker delivery is not connected");
        post_(unsigned(std::nearbyint(p[15])),int(std::nearbyint(p[16])),int(std::nearbyint(p[17])),*hit);
        const float smoke[]{0,0,0,effect.origin.x,effect.origin.y,effect.origin.z,0,0,effect.direction.x,effect.direction.y,effect.direction.z,1,1,1,.5f,p[10]*.125f,p[9]*.5f,p[9],20};
        create(0,smoke);
    }
    return false;
}
std::vector<ModelMesh> Effects::energy_projectile_geometry(const Instance& effect)const{
    // 1a2ab8: three luminous axial strips, then twelve paired radial triangles.
    constexpr float tau=2*std::numbers::pi_v<float>;const auto& p=effect.parameters;
    const float radius=p[9],length=p[10],half=length*.5f,aura=effect.aura_angle*tau/65536;
    const auto point=[&](float x,float y,float z){return effect.origin+effect.trail[0]*x+effect.trail[1]*y+effect.direction*z;};
    constexpr math::Vector4 colors[]{{1,.5f,.2f,0},{1,1,.9f,0},{1,.5f,.2f,0},{1,1,.9f,1},{1,.5f,.2f,0},{1,1,.9f,0}};
    std::vector<ModelMesh> result;const float c=std::cos(tau/64),s=std::sin(tau/64);
    for(unsigned i=0;i<3;++i){ModelMesh mesh;mesh.flags=0x48000000;
        const float angle=float((effect.aura_angle+i*65536/3)&65535)*tau/65536,x=std::cos(angle),y=std::sin(angle);
        const math::Vector4 positions[]{point((radius+s*half)*x,(radius+s*half)*y,(1+c)*half),point(0,0,length),point(radius*x,radius*y,half),point(0,0,half),point((radius-s*half)*x,(radius-s*half)*y,(1-c)*half),point(0,0,0)};
        for(unsigned j=0;j<6;++j)mesh.vertices.push_back({positions[j],{},colors[j],0,0});
        for(unsigned j=2;j<6;++j)mesh.indices.insert(mesh.indices.end(),{j-2,j-1,j});result.push_back(std::move(mesh));
    }
    ModelMesh shell;shell.flags=0x44000000;
    const math::Vector4 white{1,1,1,.125f},edge{1.9921875f,1.9921875f,1.9921875f,.125f},clear{1.9921875f,1.9921875f,1.9921875f,0},bright{1,1,1,.25f};
    for(unsigned i=0;i<12;++i){const float a=i*tau/12+aura,b=(i*2+1)*tau/24+aura,d=(i+1)*tau/12+aura;
        const float sine=std::sin(effect.particles[i].angle),cosine=std::cos(effect.particles[i].angle);
        const math::Vector4 v[]{point(radius*std::cos(a),radius*std::sin(a),half),point((radius+half*sine)*std::cos(b),(radius+half*sine)*std::sin(b),(1+cosine)*half),point(radius*std::cos(d),radius*std::sin(d),half),point((radius-half*sine)*std::cos(b),(radius-half*sine)*std::sin(b),(1-cosine)*half)};
        const unsigned base=unsigned(shell.vertices.size());shell.vertices.insert(shell.vertices.end(),{{v[0],{},white,0,0},{v[1],{},clear,0,0},{v[2],{},edge,0,0},{v[3],{},bright,0,0}});shell.indices.insert(shell.indices.end(),{base,base+1,base+2,base+2,base+3,base});
    }
    result.push_back(std::move(shell));return result;
}
}
