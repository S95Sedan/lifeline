#include "game/effects/effects.hpp"
#include <algorithm>
#include <numbers>

namespace lifeline::script {
namespace {
constexpr float tau=2*std::numbers::pi_v<float>;
int selector(float v){
    if(!std::isfinite(v)||double(v)<-2147483648.||double(v)>=2147483648.)throw std::runtime_error("Invalid wave selector");
    return int(v);
}
float turn(float turns){return float(selector(turns*65536.f)&65535)*(tau/65536.f);}
}
int Effects::create_wave(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty()&&input.size()!=13)throw std::runtime_error("Wave requires 13 parameters");
    Instance wave;wave.type=10006;auto& p=wave.parameters;
    const float defaults[]{0,2,3,0,0,5,0,1,0,0,32,2,160};
    std::copy(std::begin(defaults),std::end(defaults),p.begin());
    if(!input.empty()&&selector(input[0])==0)std::copy(input.begin(),input.end(),p.begin());
    for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Nonfinite wave parameter");
    if(p[10]<=0)throw std::runtime_error("Invalid wave extent");
    const auto parent=attachment_(selector(p[1]),selector(p[2]));
    wave.origin=math::transform(parent,{p[3],p[4],p[5],1});wave.origin.w=1;
    const auto axis=math::normalize_xyz({p[7],p[8],p[9],0});
    // 1a0fc0/1cb120: degrees -> radians -> quantized turn, normalized axis.
    const float a=turn((p[6]*.0027777778450399637f*tau)*.15915493667125702f),c=std::cos(a),s=std::sin(a);
    const auto rotate=[&](math::Vector4 v){return math::transform(parent,v*c+math::cross_xyz(axis,v)*s+axis*(math::dot_xyz(axis,v)*(1-c)));};
    wave.trail={rotate({1,0,0,0}),rotate({0,1,0,0}),rotate({0,0,1,0})};
    wave.lifetime=p[12];wave.particles.resize(16);
    for(auto& point:wave.particles){point.size=0;point.color.w=0;}
    return insert(std::move(wave));
}
bool Effects::advance_wave(Instance& wave,float frames){
    const auto& p=wave.parameters;const float extent=p[10],half=extent*.5f,spacing=extent/15.f,center=wave.age*.5f-half;
    for(unsigned i=0;i<16;++i){const float x=float(i)*spacing,distance=(x-center)/(half*.5f);
        const float weight=(1/(1+distance*distance))*std::cos(turn((x/extent)*.25f));
        wave.particles[i].color.w=weight;
        wave.particles[i].size=(std::cos(turn((x-center)/half))*p[11])*weight;
    }
    wave.age+=frames;return wave.age<=wave.lifetime;
}
std::vector<ModelMesh> Effects::wave_geometry(const Instance& wave,const SceneCamera& camera)const{
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1);
    const auto right=math::normalize_xyz(math::cross_xyz(forward,camera.up))*(camera.mirror_horizontal?-1.f:1.f);
    const auto up=math::cross_xyz(math::normalize_xyz(math::cross_xyz(forward,camera.up)),forward);
    const auto position=[&](float x,float y,float z){return wave.origin+wave.trail[0]*x+wave.trail[1]*y+wave.trail[2]*z;};
    std::array<std::array<ModelVertex,16>,16> vertices;
    for(unsigned angle=0;angle<16;++angle){const float a=float(angle)*tau/16,c=std::cos(a),s=std::sin(a);
        for(unsigned i=0;i<16;++i){const float radius=float(i)*(wave.parameters[10]/15.f),offset=wave.particles[i].size;
            const auto sample=position((radius+offset)*c,(radius+offset)*s,0)+camera.eye*-1;
            const float z=math::dot_xyz(sample,forward);
            // A vertex exactly on the eye plane has no finite projection.
            // Saturate its UV at the clamp edge; ordinary geometry clipping
            // still decides whether its adjacent triangles are visible.
            const float divisor=z==0?std::numeric_limits<float>::min():z;
            const auto projected=[&](float coordinate,float dimension){return std::clamp(coordinate*(camera.focal_pixels/dimension)/divisor,-1.e6f,1.e6f)+.5f;};
            vertices[angle][i]={position(radius*c,radius*s,offset),{},{1,1,1,wave.particles[i].color.w},projected(math::dot_xyz(sample,right),640),projected(-math::dot_xyz(sample,up),448)};
        }
    }
    std::vector<ModelMesh> meshes;meshes.reserve(16);
    for(unsigned angle=0;angle<16;++angle){ModelMesh mesh;mesh.flags=0x44000000;mesh.scene_wave=true;mesh.depth_write=false;mesh.linear_filter=true;
        for(unsigned i=0;i<16;++i){mesh.vertices.push_back(vertices[angle][i]);mesh.vertices.push_back(vertices[(angle+1)%16][i]);}
        for(unsigned i=2;i<32;++i)mesh.indices.insert(mesh.indices.end(),{i-2,i-1,i});
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}
}
