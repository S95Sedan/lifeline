#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <numbers>

namespace lifeline::script {
namespace {
constexpr float tau=2*std::numbers::pi_v<float>;
int selector(float value){
    if(!std::isfinite(value)||double(value)<-2147483648.||double(value)>=2147483648.)throw std::runtime_error("Invalid shell selector");
    return int(value);
}
float angle(float radians){return float(selector(radians*.15915493667125702f*65536.f)&65535)*(tau/65536.f);}
}
int Effects::create_shell(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty()&&input.size()!=35)throw std::runtime_error("Shell requires 35 parameters");
    Instance shell;shell.type=7000;auto& p=shell.parameters;
    const float defaults[]{0,0,0,0,0,0,0,0,0,1,0,2.5f,15,5,.5f,.7f,1,1,1,.8f,.7f,1,1,1,1,1,60,30,15,0,0,0,0,0,0};
    std::copy(std::begin(defaults),std::end(defaults),p.begin());
    if(!input.empty()&&selector(input[0])==0)std::copy(input.begin(),input.end(),p.begin());
    for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Nonfinite shell parameter");
    auto origin=math::transform(attachment_(selector(p[1]),selector(p[2])),{p[3],p[4],p[5],1});
    shell.origin=ground_?ground_(origin):math::Vector4{origin.x,0,origin.z,1};
    shell.direction=math::normalize_xyz(math::transform(attachment_(selector(p[6]),selector(p[7])),{p[8],p[9],p[10],0}));
    shell.radial_axis=math::normalize_xyz(math::cross_xyz(shell.direction,{1,0,0,0}));
    if(math::dot_xyz(shell.radial_axis,shell.radial_axis)==0)shell.radial_axis=math::normalize_xyz(math::cross_xyz(shell.direction,{0,0,1,0}));
    shell.trail[0]=math::cross_xyz(shell.radial_axis,shell.direction);
    shell.lifetime=(p[26]+p[27])+p[28];p[35]=p[11];p[36]=0;p[37]=1;p[38]=0;
    for(unsigned i:{29u,30u,32u,33u,34u})p[i]=float(selector(p[i]));
    shell.particles.resize(34);for(auto& point:shell.particles){point.position={};point.direction={};point.life=0;}
    shell.textures={texture(7000,"shellring.raw"),texture(7000,"shellthunder.raw"),texture(7000,"shellringedge.raw")};
    return insert(std::move(shell));
}
bool Effects::advance_shell(Instance& shell,float frames){
    const auto unit=[&]{return float(random_()&4095)/4095.f;};auto& p=shell.parameters;
    for(unsigned ring=0;ring<2;++ring){auto& state=shell.particles[32+ring];state.age+=frames;
        if(state.age>state.life){
            state.age-=state.life;
            for(unsigned i=0;i<16;++i){auto& point=shell.particles[ring*16+i];point.position={};
                const float x=unit()*4-2,y=unit()*4-2,z=unit()*4-2;point.direction={x,y,z,0};}
            state.life=unit()*7+1;state.growth=unit()*.5f+.5f;state.angle=unit()*tau;state.size=float(random_()&3)*.25f;
        }else for(unsigned i=0;i<16;++i){auto& point=shell.particles[ring*16+i];point.position=point.position*state.growth+point.direction*(1-state.growth);}
    }
    if(shell.age<p[26]){
        const float growth=1-1/(1+shell.age/p[26]*10);p[35]=p[11];p[36]=p[13]*growth;p[37]=growth;
    }else if(shell.age<p[26]+p[27]){
        const float t=(shell.age-p[26])/p[27];p[36]=p[13];p[35]=p[11]+(p[12]-p[11])*(1-1/(1+10*t));p[37]=1-t;
        if(selector(p[29])){
            if(!overlap_)throw std::runtime_error("Shell collision service is not connected");
            const auto mask=std::uint32_t(std::clamp(double(p[31]),0.,4294967295.));
            const auto material=overlap_(shell.origin+shell.direction*p[13],p[35],selector(p[30]),std::bit_cast<int>(mask));
            if(material){if(selector(p[32])>=0){
                if(!post_)throw std::runtime_error("Shell worker delivery is not connected");
                post_(unsigned(selector(p[32])),selector(p[33]),selector(p[34]),*material);}
                p[29]=0;
            }
        }
    }else p[37]=0;
    p[38]+=.006135923322290182f;if(p[38]>tau)p[38]-=tau;
    shell.age+=frames;return shell.age<=shell.lifetime;
}
std::vector<ModelMesh> Effects::shell_geometry(const Instance& shell,std::array<unsigned,3> textures)const{
    std::vector<ModelMesh> meshes;if(!shell.textures[0])return meshes;
    const auto& p=shell.parameters;
    const auto position=[&](float x,float y,float z,float rotation){const float a=angle(rotation),c=std::cos(a),s=std::sin(a);
        return shell.origin+shell.trail[0]*(c*x+s*z)+shell.direction*y+shell.radial_axis*(-s*x+c*z);};
    const auto make=[&](unsigned texture){ModelMesh m;m.texture=texture;m.flags=0x48000000;m.repeat_u=true;return m;};
    const auto strip=[&](ModelMesh m){for(unsigned i=2;i<m.vertices.size();++i)m.indices.insert(m.indices.end(),{i-2,i-1,i});meshes.push_back(std::move(m));};
    const auto color=[&](unsigned i){return math::Vector4{p[i],p[i+1],p[i+2],p[i+3]*p[37]};};
    if(shell.age>p[26]){
        auto fan=make(textures[0]);const auto c=color(22);fan.vertices.push_back({position(0,p[13],0,0),{},c,.5f,.5f});
        for(unsigned i=0;i<=32;++i){const float a=float(i)*tau/32,x=std::cos(a),z=std::sin(a);
            fan.vertices.push_back({position(x*p[35],p[36],z*p[35],0),{},c,x*.5f+.5f,z*.5f+.5f});
            if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
        meshes.push_back(std::move(fan));
    }
    for(unsigned orientation=0;orientation<2;++orientation){auto edge=make(textures[2]);const auto c=color(14);
        for(unsigned i=0;i<=32;++i){const float a=float(i)*tau/32,x=std::cos(a),z=std::sin(a);
            for(unsigned side=0;side<2;++side){const float offset=side?1.f:-1.f;
                const float radius=p[35]+(orientation?0:offset),height=p[36]+(orientation?offset:0);
                edge.vertices.push_back({position(x*radius,height,z*radius,orientation?-p[38]:p[38]),{},c,float(i)*.5f,float(side)});}}
        strip(std::move(edge));
    }
    if(shell.age>p[26])for(unsigned ring=0;ring<2;++ring){const auto& state=shell.particles[32+ring];auto ribbon=make(textures[1]);const auto c=color(18);
        for(unsigned i=0;i<16;++i){const float a=float(i)*tau/16;const auto& offset=shell.particles[ring*16+i].position;
            for(unsigned side=0;side<2;++side)ribbon.vertices.push_back({position(std::cos(a)*p[35]+offset.x,p[36]+offset.y+(side?-3.f:3.f),std::sin(a)*p[35]+offset.z,state.angle),{},c,float(i)*.5f,state.size+float(side)*.25f});}
        strip(std::move(ribbon));
    }
    return meshes;
}
}
