#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <numbers>

namespace lifeline::script {
namespace {
using V=math::Vector4;
int number(float x){return int(std::nearbyint(x));}
V vector(std::span<const float> p,unsigned i,float w=0){return {p[i],p[i+1],p[i+2],w};}
void store(std::span<float> p,unsigned i,V v){p[i]=v.x;p[i+1]=v.y;p[i+2]=v.z;}
float length(V v){return std::sqrt(math::dot_xyz(v,v));}
unsigned samples(float distance,float step){if(!(step>0))return distance>0?8:1;return unsigned(std::clamp(std::nearbyint(distance/step)+1,1.f,8.f));}
}
int Effects::create_plant(std::span<const float> input){
    // 19c368: attached glow, observer crossing, and three independent impact children.
    if(!input.empty()&&input.size()!=76)throw std::runtime_error("Plant effect requires 76 parameters");
    Instance effect;effect.type=6000;effect.composite.emplace();auto& c=*effect.composite;auto& p=c.input;
    constexpr float initial[]{0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,1,1,2,60,4,15,60,30};
    std::copy(std::begin(initial),std::end(initial),p.begin());if(!input.empty())std::copy(input.begin(),input.end(),p.begin());
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Invalid plant parameter");
    effect.origin=math::transform(attachment_(number(p[1]),number(p[2])),vector(p,3,1));
    c.previous=math::transform(attachment_(number(p[6]),number(p[7])),vector(p,8,1));
    c.radius_to=p[20];c.alpha_to=p[19];c.duration=p[21];
    effect.particles.resize(8);for(auto& point:effect.particles){point.position=effect.origin;point.color.w=0;}
    effect.textures[0]=texture(6000,"pointlight.raw");return insert(std::move(effect));
}
int Effects::create_plant_smoke(std::span<const float> input,bool slowed){
    const int slot=create(0,input);if(slot<0)return slot;
    auto& effect=*slots_[slot];
    if(slowed){effect.textures[0]=texture(6000,"plantsmoke.raw");effect.type=-67;}return slot;
}
bool Effects::advance_plant(Instance& effect,float frames){
    auto& c=*effect.composite;auto& p=c.input;
    // 19c8e0 deliberately tests completion before advancing the fade clock.
    if(c.time<c.duration)c.time+=frames;
    else if(c.dying){if(number(p[26])&&number(p[27])>=0&&post_)post_(number(p[27]),number(p[28]),number(p[29]),number(p[30]));return false;}
    else c.time=c.duration;
    const float t=c.duration!=0?c.time/c.duration:1;
    c.radius=c.radius_from+(c.radius_to-c.radius_from)*t;c.alpha=c.alpha_from+(c.alpha_to-c.alpha_from)*t;
    effect.origin=math::transform(attachment_(number(p[1]),number(p[2])),vector(p,3,1));
    c.trail_time+=frames;
    while(c.trail_time>=1){for(unsigned i=7;i>0;--i){effect.particles[i]=effect.particles[i-1];effect.particles[i].color.w=std::max(0.f,effect.particles[i].color.w-1.f/3);}
        effect.particles[0].position=effect.origin;effect.particles[0].size=c.radius;effect.particles[0].color.w=1;c.trail_time-=1;}
    const V current=math::transform(attachment_(number(p[6]),number(p[7])),vector(p,8,1));
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    const auto gravity=[&](V v){return gravity_?gravity_(v):V{0,-1,0,0};};
    if(c.active&&c.radius>=p[22]){
        const unsigned n=samples(length(effect.particles[0].position+effect.particles[1].position*(-1)),c.radius*.5f);
        for(unsigned i=0;i<n;++i)if(unit()>.5f){
            std::array<float,19> smoke{};const float t=float(i)/n;
            const V position=effect.particles[0].position*(1-t)+effect.particles[1].position*t;
            store(smoke,3,position);store(smoke,8,gravity(position)*(-1));std::copy_n(p.begin()+31,4,smoke.begin()+11);
            smoke[15]=p[35]+unit()*(p[36]-p[35]);smoke[16]=p[37];smoke[17]=unit()*(p[38]-p[37]);smoke[18]=p[39]+unit()*(p[40]-p[39]);create_plant_smoke(smoke,true);
        }
        const auto height=[&](V v){return ground_height_?ground_height_(v):v.y;};
        if(height(c.previous)>0&&height(current)<0){
            const V reference=math::transform(attachment_(number(p[11]),number(p[12])),vector(p,13,1));
            const V normal=gravity(current)*(-1),direction=math::normalize_xyz(current+reference*(-1));
            const V side=math::normalize_xyz(math::cross_xyz(direction,normal));const float distance=length(current+c.previous*(-1));
            const V travel=math::cross_xyz(normal,side)*distance;
            const V position=(ground_?ground_(current):V{current.x,0,current.z,1})+normal;
            std::array<float,44> pillar{};store(pillar,3,position);store(pillar,8,travel);pillar[11]=p[45];pillar[12]=p[46];pillar[13]=distance*2;pillar[14]=distance*8;
            std::copy_n(p.begin()+47,29,pillar.begin()+15);create_plant_pillar(pillar);
            std::array<float,21> debris{};store(debris,3,position);store(debris,6,normal);debris[9]=std::numbers::pi_v<float>;
            std::copy_n(p.begin()+16,4,debris.begin()+10);debris[14]=distance*.5f;debris[15]=distance;debris[16]=p[41];debris[17]=p[42];debris[18]=30;debris[19]=60;debris[20]=32;create_debris(debris);
            std::array<float,13> flash{};store(flash,3,position);flash[6]=p[43];flash[7]=p[44];std::copy_n(p.begin()+16,4,flash.begin()+8);flash[12]=30;create(11,flash);
        }
    }
    c.previous=current;return true;
}
int Effects::create_plant_pillar(std::span<const float> input){
    // 19db68, vtable 30b5f8. Kept independent of the emitter's lifetime.
    if(!input.empty()&&input.size()!=44)throw std::runtime_error("Plant pillar requires 44 parameters");
    Instance effect;effect.type=6001;effect.composite.emplace();auto& p=effect.composite->input;
    constexpr float defaults[]{0,2,12,0,0,0,2,3,0,0,1,10,15,2,4,1,1,1,1,.5f,.7f,1,1,.5f,.7f,1,.5f,60,1.5f,1.5f,1.5f,.25f,.1f,.2f,2,7,15,30,0,0,0,0,0,0};
    std::copy(std::begin(defaults),std::end(defaults),p.begin());if(!input.empty()&&number(input[0])==0)std::copy(input.begin(),input.end(),p.begin());
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Invalid plant pillar parameter");
    effect.origin=math::transform(attachment_(number(p[1]),number(p[2])),vector(p,3,1));
    effect.direction=math::transform(attachment_(number(p[6]),number(p[7])),vector(p,8));effect.lifetime=p[27];
    effect.particles.resize(16);for(auto& point:effect.particles){point.position=effect.origin;point.color.w=0;}
    effect.textures[0]=texture(6001,"plantpillar.raw");effect.textures[1]=texture(6001,"plantball.raw");return insert(std::move(effect));
}
bool Effects::advance_plant_pillar(Instance& effect,float frames){
    const auto& p=effect.composite->input;auto& trail=effect.particles;
    for(unsigned i=15;i>0;--i){trail[i]=trail[i-1];trail[i].size=std::max(0.f,trail[i].size+trail[i].growth);trail[i].color.w=std::max(0.f,trail[i].color.w-1.f/16);}
    const auto unit=[&]{return float(random_()&4095)/4095.f;};auto& head=trail[0];head.position=head.position+effect.direction;effect.origin=head.position;
    head.size=p[11]+unit()*(p[12]-p[11]);head.growth=-head.size/16;head.color.w=1;head.angle=float(random_()&1)*.5f;
    if(effect.age>effect.lifetime-16){head.color.w=std::clamp((effect.lifetime-effect.age)/16,0.f,1.f);head.size*=head.color.w;}
    if(unit()>.5f){
        const V axis=math::normalize_xyz(effect.direction);const float a=std::numbers::pi_v<float>*.25f-unit()*std::numbers::pi_v<float>*.5f;
        const float turn=float(number(a*.15915493667125702f*65536.f)&65535)*(2*std::numbers::pi_v<float>/65536.f);const V up{0,1,0,0};
        const V direction=up*std::cos(turn)+math::cross_xyz(axis,up)*std::sin(turn)+axis*(math::dot_xyz(axis,up)*(1-std::cos(turn)));
        std::array<float,19> smoke{};store(smoke,3,head.position);store(smoke,8,direction);std::copy_n(p.begin()+28,4,smoke.begin()+11);
        smoke[16]=p[34];smoke[17]=unit()*(p[35]-p[34]);smoke[15]=p[32]+unit()*(p[33]-p[32]);smoke[18]=p[36]+unit()*(p[37]-p[36]);create_plant_smoke(smoke,false);
    }
    if(number(p[38])&&capsule_contact_){
        const auto hit=capsule_contact_(head.position,head.position+V{0,head.size,0,0},p[13],number(p[39]),int(std::uint32_t(std::max(0.,std::trunc(double(p[40]))))));
        if(hit){if(number(p[41])>=0&&post_)post_(number(p[41]),number(p[42]),number(p[43]),*hit);effect.composite->active=true;}
    }
    effect.age+=frames;return effect.age<=effect.lifetime;
}
std::vector<ModelMesh> Effects::plant_geometry(const Instance& effect,const SceneCamera& camera,std::array<unsigned,3> textures)const{
    const auto& c=*effect.composite;const auto& p=c.input;std::vector<ModelMesh> output;
    const V forward=math::normalize_xyz(camera.target+camera.eye*(-1)),right=math::normalize_xyz(math::cross_xyz(camera.up,forward)),up=math::cross_xyz(forward,right);
    const auto quad=[&](V center,V h,V v,V color,unsigned texture){ModelMesh m;m.texture=texture;m.flags=0x48000000;
        m.vertices={{center+h*(-1)+v*(-1),{},color,0,0},{center+h+v*(-1),{},color,1,0},{center+h*(-1)+v,{},color,0,1},{center+h+v,{},color,1,1}};m.indices={0,1,2,1,2,3};output.push_back(std::move(m));};
    if(effect.type==6000){
        for(unsigned i=0;i<3;++i){const auto& a=effect.particles[i];const auto& b=effect.particles[i+1];const unsigned n=samples(length(a.position+b.position*(-1)),c.radius*std::bit_cast<float>(0x3eaaa64cu));
            for(unsigned j=0;j<n;++j){const float t=float(j)/n;quad(a.position*(1-t)+b.position*t,right*a.size,up*a.size,{p[16],p[17],p[18],c.alpha*(a.color.w*(1-t)+b.color.w*t)},textures[0]);}}
        // 19f49c: eight-sided fan, with its center one radius toward the eye.
        const V z=math::normalize_xyz(effect.origin+camera.eye*(-1));V x=math::normalize_xyz(math::cross_xyz(z,{0,1,0,0}));if(length(x)==0)x=math::normalize_xyz(math::cross_xyz(z,{1,0,0,0}));const V y=math::cross_xyz(x,z);
        ModelMesh fan;fan.texture=textures[0];fan.flags=0x48000000;const V color{p[16],p[17],p[18],c.alpha};fan.vertices.push_back({effect.origin+z*(-c.radius),{},color,.5f,.5f});
        for(unsigned i=0;i<=8;++i){const float a=i*std::numbers::pi_v<float>/4;fan.vertices.push_back({effect.origin+x*(std::cos(a)*c.radius)+y*(std::sin(a)*c.radius),{},color,.5f+std::cos(a)*.5f,.5f+std::sin(a)*.5f});if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}output.push_back(std::move(fan));
    }else{
        for(const auto& point:effect.particles){ModelMesh m;m.texture=textures[0];m.flags=0x48000000;const V high{p[19],p[20],p[21],p[22]*point.color.w},low{p[15],p[16],p[17],p[18]*point.color.w};
            const V top=point.position+effect.direction*(-2)+V{0,point.size,0,0},h=right*p[13];
            m.vertices={{top+h*(-1),{},high,point.angle,0},{point.position+h*(-1),{},low,point.angle,1},{top+h,{},high,point.angle+.5f,0},{point.position+h,{},low,point.angle+.5f,1}};m.indices={0,1,2,1,2,3};output.push_back(std::move(m));}
        for(const auto& point:effect.particles)quad(point.position+V{0,.3f,0,0},{p[14],0,0,0},{0,0,p[14],0},{p[23],p[24],p[25],p[26]*point.color.w},textures[1]);
    }
    return output;
}
}
