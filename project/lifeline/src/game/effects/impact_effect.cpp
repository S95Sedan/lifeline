#include "game/effects/effects.hpp"
#include <algorithm>
#include <numbers>

namespace lifeline::script {
namespace {
constexpr float pi=std::numbers::pi_v<float>,tau=2*pi;
int integer(float value){if(!std::isfinite(value)||double(value)<-2147483648.||double(value)>=2147483648.)throw std::runtime_error("Invalid impact integer");return int(value);}
math::Vector4 vector(std::span<const float> p,unsigned i,float w=0){return {p[i],p[i+1],p[i+2],w};}
math::Matrix4 rotate(math::Matrix4 basis,unsigned axis,float radians){
    const float angle=float(unsigned(integer(radians*.15915493667125702f*65536.f))&65535)*(tau/65536.f),c=std::cos(angle),s=std::sin(angle);
    auto rotation=math::identity();const auto original=rotation;const unsigned u=(axis+1)%3,v=(axis+2)%3;
    rotation.columns[u]=original.columns[u]*c+original.columns[v]*s;rotation.columns[v]=original.columns[v]*c+original.columns[u]*(-s);
    return math::multiply(basis,rotation);
}
}
// 1813a8: an expanding ring owns its lifetime while spark/debris/flash
// children occupy independent effect slots, in that order.
int Effects::create_impact(std::span<const float> input){
    if(count()==slots_.size())return -1;
    std::array<float,20> p{0,0,0,0,25,0,0,1,0,pi,5,.95f,.71f,.14f,1,.14f,.88f,.7f,1,60};
    if(!input.empty()){if(input.size()!=p.size())throw std::runtime_error("Impact requires 20 parameters");std::copy(input.begin(),input.end(),p.begin());}
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Nonfinite impact parameter");
    if(integer(p[0])!=0)return -1;
    std::array<float,15> ring{};std::copy_n(p.begin()+1,8,ring.begin()+1);ring[9]=p[10];ring[10]=ring[11]=ring[12]=ring[13]=1;ring[14]=p[19];
    const int handle=create(15,ring);if(handle<0)return handle;
    auto& parent=*slots_[handle];parent.type=13;parent.textures[0]=texture(13,"explosion2ring.raw");
    if(!parent.textures[0]){slots_[handle].reset();throw std::runtime_error("Impact ring texture is not installed");}
    const float t=1/parent.lifetime,step=(1-(1-t)/(1+4*t))*p[10];
    // Ring initialization stores its normalized direction at +20.
    const auto normal=math::normalize_xyz(vector(p,6));
    std::array<float,21> spark{};std::copy_n(p.begin()+1,5,spark.begin()+1);
    spark[6]=normal.x;spark[7]=normal.y;spark[8]=normal.z;spark[9]=pi;
    std::copy_n(p.begin()+11,4,spark.begin()+10);spark[14]=2;spark[16]=step*.5f;spark[17]=.1f;spark[18]=step;spark[19]=p[19]*.5f;spark[20]=p[19];
    create(3,spark);
    std::array<float,21> debris{};std::copy_n(spark.begin(),10,debris.begin());std::copy_n(p.begin()+15,4,debris.begin()+10);
    debris[15]=step*.5f;debris[16]=.1f;debris[17]=step*.5f;debris[18]=p[19]*.5f;debris[19]=p[19];debris[20]=16;
    create_debris(debris);
    std::array<float,13> flash{};std::copy_n(p.begin()+1,5,flash.begin()+1);flash[6]=p[10];flash[7]=2*p[10];flash[8]=flash[9]=flash[10]=1;flash[11]=.5f;flash[12]=32;create(11,flash);
    return handle;
}
// 199ff8/19a318: allocation splitting is unnecessary for native vectors;
// preserve total particle count and random draws across both original pools.
int Effects::create_debris(std::span<const float> input){
    if(count()==slots_.size())return -1;
    std::array<float,21> p{0,2,3,0,0,0,0,0,1,pi*.5f,1,0,0,1,.5f,.6f,.2f,.5f,60,120,32};
    if(!input.empty()){if(input.size()!=p.size())throw std::runtime_error("Debris requires 21 parameters");if(integer(input[0])==0)std::copy(input.begin(),input.end(),p.begin());}
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Nonfinite debris parameter");
    const int count=std::max(1,integer(p[20]));if(count>65536)throw std::runtime_error("Debris allocation exceeds native particle limit");
    const auto attachment=attachment_(integer(p[1]),integer(p[2]));
    Instance effect;effect.type=2;effect.origin=math::transform(attachment,vector(p,3,1));effect.radial_axis=gravity_?gravity_(effect.origin):math::Vector4{0,-1,0,0};
    auto basis=math::identity();basis.columns[2]=math::normalize_xyz(math::transform(attachment,vector(p,6)));
    basis.columns[0]=math::normalize_xyz(math::cross_xyz(basis.columns[2],{0,1,0,0}));
    if(math::dot_xyz(basis.columns[0],basis.columns[0])==0)basis.columns[0]=math::normalize_xyz(math::cross_xyz(basis.columns[2],{1,0,0,0}));
    basis.columns[1]=math::cross_xyz(basis.columns[0],basis.columns[2]);
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    for(int i=0;i<count;++i){
        Particle particle;particle.position=effect.origin;particle.color=vector(p,10,p[13]);
        auto axes=rotate(basis,2,unit()*tau);axes=rotate(axes,0,unit()*p[9]-p[9]*.5f);
        particle.direction=axes.columns[2]*(p[14]+(p[15]-p[14])*unit());
        particle.rotation={unit()*tau-pi,unit()*tau-pi,unit()*tau-pi,0};
        particle.angular_velocity={unit()*tau-pi,unit()*tau-pi,unit()*tau-pi,0};
        particle.life=p[18]+(p[19]-p[18])*unit();particle.size=p[16]+(p[17]-p[16])*unit();effect.particles.push_back(particle);
    }
    return insert(std::move(effect));
}
bool Effects::advance_debris(Instance& effect,float frames){
    bool live=false;
    for(auto& p:effect.particles)if(p.age<=p.life){live=true;p.age+=frames;p.position=p.position+p.direction;p.rotation=p.rotation+p.angular_velocity;
        const auto wrap=[](float a){if(a>pi)a-=tau;if(a< -pi)a+=tau;return a;};p.rotation={wrap(p.rotation.x),wrap(p.rotation.y),wrap(p.rotation.z),0};
        p.direction=p.direction+effect.radial_axis*.01f;
    }
    return live;
}
ModelMesh Effects::debris_geometry(const Instance& effect)const{
    ModelMesh mesh;mesh.texture=0;mesh.flags=0x44000000;
    for(const auto& p:effect.particles)if(p.age<=p.life&&p.life>0){
        // 19aaec scales stored angles directly by 65536 (not radians/tau).
        const auto angle=[](float a){return float(unsigned(integer(a*65536.f))&65535)*(tau/65536.f);};
        const float x=angle(p.rotation.x),y=angle(p.rotation.y),z=angle(p.rotation.z);
        const float cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
        const math::Matrix4 matrix{{math::Vector4{cy*cz,sx*cz*sy-cx*sz,cx*cz*sy+sx*sz,0},{cy*sz,cx*cz+sx*sz*sy,cx*sz*sy-sx*cz,0},{-sy,sx*cy,cx*cy,0},p.position}};
        const float size=(1-p.age/p.life)*p.size;const unsigned base=unsigned(mesh.vertices.size());
        for(auto point:{math::Vector4{0,-size,0,1},math::Vector4{size,size,0,1},math::Vector4{-size,size,0,1}})mesh.vertices.push_back({math::transform(matrix,point),{},p.color,0,0});
        mesh.indices.insert(mesh.indices.end(),{base,base+1,base+2});
    }
    return mesh;
}
}
