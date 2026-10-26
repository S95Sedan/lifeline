#include "game/effects/effects.hpp"
#include <algorithm>
#include <numbers>
namespace lifeline::script {
int Effects::create_energy_trail(std::span<const float> input){
    // 16d418: eight delayed wave fronts and a 32-segment spiral ribbon.
    if(!input.empty()&&input.size()!=15)throw std::runtime_error("Energy trail requires 15 parameters");
    if(count()==slots_.size())return -1;
    Instance effect;effect.type=10005;constexpr float defaults[]{0,2,3,0,0,0,0,0,1,10,1,4,1,.5f,120};
    std::copy(std::begin(defaults),std::end(defaults),effect.parameters.begin());
    if(!input.empty()&&std::nearbyint(input[0])==0)std::copy(input.begin(),input.end(),effect.parameters.begin());
    auto& p=effect.parameters;for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid energy trail parameter");
    const auto parent=attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2])));
    effect.origin=math::transform(parent,{p[3],p[4],p[5],1});effect.direction=math::normalize_xyz(math::transform(parent,{p[6],p[7],p[8],0}));
    effect.trail[0]=math::normalize_xyz(math::cross_xyz(effect.direction,{0,1,0,0}));effect.trail[1]=math::cross_xyz(effect.trail[0],effect.direction);effect.trail[2]=effect.direction;
    effect.particles.resize(40);for(unsigned i=0;i<8;++i){auto& front=effect.particles[i];front.position=effect.origin;front.age=-float(i);front.size=p[9];front.speed=p[11];}
    effect.lifetime=p[14];p[20]=p[12];p[21]=p[13];p[22]=1;
    effect.textures[0]=texture(10005,"lee_ring.raw");return insert(std::move(effect));
}
bool Effects::advance_energy_trail(Instance& effect,float frames){
    auto& p=effect.parameters;constexpr float tau=2*std::numbers::pi_v<float>;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(unsigned i=0;i<8;++i){auto& front=effect.particles[i];
        if(front.age<0)front.age+=frames;
        else{front.age+=frames;const float momentum=front.size*front.speed;front.size=front.size*.9f+p[10]*.1f;front.speed=momentum/front.size;front.position=front.position+effect.direction*(front.speed*.125f);}
        front.angle=float((random_()<<4)&65535)*(tau/65536);front.extent=2*front.size*unit();
    }
    const unsigned phase=random_()<<4;
    for(unsigned i=0;i<32;++i){auto& point=effect.particles[8+i];const float t=i/32.f,weight=1-t;
        const float a=float((phase+i*4096)&65535)*(tau/65536);
        const float radius=(1-weight*weight)*(.5f+.5f*unit())*effect.particles[i/4].size;
        point.position=effect.particles[0].position*weight+effect.particles[7].position*t+effect.trail[0]*(radius*std::cos(a))+effect.trail[1]*(radius*std::sin(a));
        point.direction=effect.trail[0]*std::cos(a)+effect.trail[1]*std::sin(a);
    }
    p[20]+=p[21];p[21]*=.95f;p[22]=p[21]/p[13];effect.age+=frames;return effect.age<=effect.lifetime;
}
std::vector<ModelMesh> Effects::energy_trail_geometry(const Instance& effect,const SceneCamera& camera,unsigned texture)const{
    constexpr float tau=2*std::numbers::pi_v<float>;std::vector<ModelMesh> result;
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));const auto right=math::normalize_xyz(math::cross_xyz(forward,{0,1,0,0}));const auto up=math::cross_xyz(right,forward);
    const auto mesh=[](){ModelMesh m;m.flags=0x48000000;return m;};
    for(unsigned i=0;i<8;++i){const auto& front=effect.particles[i];if(front.age<0)continue;
        const auto point=[&](float x,float y,float z){return front.position+effect.trail[0]*x+effect.trail[1]*y+effect.direction*z;};
        for(float sign:{1.f,-1.f}){auto fan=mesh();const math::Vector4 center{.8f,1,1,.5f},edge{.2f,.8f,1,0};
            fan.vertices={{front.position,{},center,0,0},{point(0,0,front.size),{},edge,0,0},{point(sign*std::cos(front.angle)*front.size,sign*std::sin(front.angle)*front.size,0),{},edge,0,0},{point(0,0,front.size-front.speed),{},edge,0,0}};
            fan.indices={0,1,2,0,2,3};result.push_back(std::move(fan));
        }
        auto disk=mesh();disk.vertices.push_back({front.position,{},{1,1,1,.5f},0,0});
        for(unsigned j=0;j<=8;++j){const float a=j*tau/8;disk.vertices.push_back({front.position+right*(std::cos(a)*front.size)+up*(std::sin(a)*front.size),{},{0,0,1,0},0,0});if(j)disk.indices.insert(disk.indices.end(),{0,j,j+1});}
        result.push_back(std::move(disk));
    }
    auto ring=mesh();ring.texture=texture;const auto h=effect.trail[0]*effect.parameters[20],v=effect.trail[1]*effect.parameters[20];const math::Vector4 color{0,1,1,effect.parameters[22]};
    ring.vertices={{effect.origin+h*(-1)+v,{},color,0,0},{effect.origin+h*(-1)+v*(-1),{},color,0,1},{effect.origin+h+v,{},color,1,0},{effect.origin+h+v*(-1),{},color,1,1}};ring.indices={0,1,2,1,2,3};result.push_back(std::move(ring));
    auto ribbon=mesh();for(unsigned i=0;i<32;++i){const auto& point=effect.particles[8+i];ribbon.vertices.push_back({point.position,{},{0,1,1,1},0,0});ribbon.vertices.push_back({point.position+point.direction,{},{0,0,1,0},0,0});}
    for(unsigned i=2;i<ribbon.vertices.size();++i)ribbon.indices.insert(ribbon.indices.end(),{i-2,i-1,i});result.push_back(std::move(ribbon));return result;
}
}
