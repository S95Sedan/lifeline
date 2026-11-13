#include "game/effects/effects.hpp"
#include "game/transform_motion.hpp"
#include <algorithm>
#include <numbers>

namespace lifeline::script {
int Effects::create_lightning_emitter(std::span<const float> input){
    // 192018: persistent emitter, independently fading sixteen-point children.
    if(!input.empty()&&input.size()!=24)throw std::runtime_error("Lightning emitter requires 24 parameters");
    Instance effect;effect.type=5001;
    constexpr float defaults[]{0,0,0,0,10,0,0,0,0,1,0,2,5,20,10,1,1,1,1,1,8,2,60,60};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty())std::copy(input.begin(),input.end(),effect.parameters.begin());
    for(float v:effect.parameters)if(!std::isfinite(v))throw std::runtime_error("Invalid lightning emitter parameter");
    effect.textures[0]=texture(5001,"heronthunder3.raw");return insert(std::move(effect));
}
bool Effects::advance_lightning_emitter(Instance& effect,float frames){
    auto& p=effect.parameters;float scale=1;
    if(p[27]!=0){scale=1-effect.age/p[23];effect.age+=frames;if(effect.age>=p[23])return false;}
    else if(effect.age<p[22]){scale=effect.age/p[22];effect.age+=frames;}
    p[24]=p[11]*scale;p[25]=p[12]*scale;p[26]=p[18]*scale;
    p[28]+=frames;if(p[28]>=p[21]){
        p[28]-=p[21];std::array<float,21> child;std::copy_n(p.begin(),21,child.begin());
        child[11]=p[24];child[12]=p[25];child[18]=p[26];create_lightning_ribbon(child,effect.textures[0]);
    }
    return true;
}
int Effects::create_lightning_ribbon(std::span<const float> p,std::shared_ptr<const AssetImage> bitmap){
    // 18f480 / 18f9f0. Local Y follows the supplied direction; each child
    // takes its attachment snapshot once, then only its opacity changes.
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=-66;effect.textures[0]=std::move(bitmap);
    auto basis=attachment_(int(std::nearbyint(p[6])),int(std::nearbyint(p[7])));
    if(std::nearbyint(p[0])==0){
        const auto y=math::normalize_xyz(math::transform(basis,{p[8],p[9],p[10],0}));
        auto z=math::normalize_xyz(math::cross_xyz(y,{1,0,0,0}));
        if(math::dot_xyz(z,z)==0)z=math::normalize_xyz(math::cross_xyz(y,{0,1,0,0}));
        basis.columns[0]=math::cross_xyz(z,y);basis.columns[1]=y;basis.columns[2]=z;
    }else if(std::nearbyint(p[0])==1){
        for(int axis:{2,1,0}){TransformMotion rotation;const float angle=p[8+axis]*std::numbers::pi_v<float>/180;
            rotation.rotation=axis==0?math::Vector4{angle,0,0,0}:axis==1?math::Vector4{0,angle,0,0}:math::Vector4{0,0,angle,0};basis=math::multiply(basis,rotation.matrix());}
    }
    const auto origin=attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2])));
    basis.columns[3]=math::transform(origin,{p[3],p[4],p[5],1});
    constexpr float tau=2*std::numbers::pi_v<float>;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    TransformMotion rotation;rotation.rotation.y=unit()*tau;basis=math::multiply(basis,rotation.matrix());
    effect.origin=basis.columns[3];for(unsigned i=0;i<3;++i)effect.trail[i]=basis.columns[i];
    for(unsigned i=0;i<16;++i){Particle point;const float angle=i*tau/8,radius=p[11]+unit()*(p[12]-p[11]);
        const float height=p[14]+(i+2*unit()-1)*(p[13]-p[14])*.0625f;
        point.position={radius*std::cos(angle),height,radius*std::sin(angle),1};effect.particles.push_back(point);
    }
    effect.parameters[0]=float(random_()&3)*.25f;
    effect.parameters[1]=(p[11]+p[12])*tau*.03125f;
    effect.parameters[2]=p[15];effect.parameters[3]=p[16];effect.parameters[4]=p[17];effect.parameters[5]=p[18];
    effect.lifetime=p[19]+unit()*(p[20]-p[19]);return insert(std::move(effect));
}
ModelMesh Effects::lightning_ribbon_geometry(const Instance& effect,unsigned texture)const{
    ModelMesh result;result.texture=texture;result.flags=0x48000000;
    const auto& p=effect.parameters;const math::Vector4 color{p[2],p[3],p[4],p[5]*(1-effect.age/effect.lifetime)};
    for(unsigned i=0;i<16;++i){const auto point=effect.particles[i].position;
        for(unsigned edge=0;edge<2;++edge){const auto position=effect.origin+effect.trail[0]*point.x+effect.trail[1]*(point.y+(edge?-p[1]:p[1]))+effect.trail[2]*point.z;
            result.vertices.push_back({position,{},color,p[0]+edge*.25f,i*.125f});}
    }
    for(unsigned i=2;i<result.vertices.size();++i)result.indices.insert(result.indices.end(),{i-2,i-1,i});return result;
}
}
