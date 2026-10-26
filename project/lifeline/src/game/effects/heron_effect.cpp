#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <numbers>

namespace lifeline::script {
namespace {
using V=math::Vector4;
constexpr float tau=2*std::numbers::pi_v<float>;
int number(float x){return int(std::nearbyint(x));}
V vector(std::span<const float> p,unsigned i,float w=0){return {p[i],p[i+1],p[i+2],w};}
float length(V v){return std::sqrt(math::dot_xyz(v,v));}
math::Matrix4 basis(V direction,V origin){
    auto m=math::identity();if(length(direction)!=0){m.columns[2]=math::normalize_xyz(direction);m.columns[0]=math::normalize_xyz(math::cross_xyz(m.columns[2],{0,1,0,0}));
        if(length(m.columns[0])==0)m.columns[0]=math::normalize_xyz(math::cross_xyz(m.columns[2],{1,0,0,0}));m.columns[1]=math::cross_xyz(m.columns[0],m.columns[2]);}
    m.columns[3]=origin;return m;
}
math::Matrix4 rotate(math::Matrix4 m,unsigned axis,float radians){
    const float a=float(number(radians*.15915493667125702f*65536.f)&65535)*(tau/65536.f);auto r=math::identity();unsigned u=(axis+1)%3,v=(axis+2)%3;
    r.columns[u]=m.columns[u]*std::cos(a)+m.columns[v]*std::sin(a);r.columns[v]=m.columns[v]*std::cos(a)+m.columns[u]*(-std::sin(a));m.columns[u]=r.columns[u];m.columns[v]=r.columns[v];return m;
}
}
int Effects::create_heron(std::span<const float> input){
    // 18e948: reserve parent before its independently owned ball and lightning.
    if(!input.empty()&&input.size()!=34)throw std::runtime_error("Heron effect requires 34 parameters");
    Instance effect;effect.type=5000;effect.composite.emplace();auto& p=effect.composite->input;
    constexpr float defaults[]{0,2,3,0,0,0,2,3,0,-1,0,2,3,0,0,0,0,0,0,2,5,0,5,60,60,60,60,30,0,0,0,0,0,0};
    std::copy(std::begin(defaults),std::end(defaults),p.begin());if(!input.empty()&&number(input[0])==0)std::copy(input.begin(),input.end(),p.begin());
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Invalid Heron parameter");
    const auto values=p;const int result=insert(std::move(effect));if(result<0)return -1;
    Instance ball;ball.type=-68;ball.composite.emplace();auto& b=*ball.composite;
    std::copy_n(values.begin(),6,b.input.begin());std::copy_n(values.begin()+26,8,b.input.begin()+8);
    ball.origin=math::transform(attachment_(number(values[1]),number(values[2])),vector(values,3,1));
    ball.particles.resize(16);for(auto& point:ball.particles){point.position=ball.origin;point.color.w=0;}
    ball.textures[0]=texture(5000,"heronball.raw");
    slots_[result]->composite->ball=insert(std::move(ball));
    const std::array<float,24> lightning{1,values[11],values[12],values[13],values[14],values[15],values[11],values[12],values[16],values[17],values[18],values[19],values[20],values[21],values[22],0,.5f,1,1,1,8,2,values[23]*.5f,values[25]};
    slots_[result]->composite->lightning=create_lightning_emitter(lightning);return result;
}
void Effects::cancel_heron(Instance& effect){
    // 18f180 ignores the command number. Children fade only once.
    auto& c=*effect.composite;
    if(effect.age<c.input[25]){
        if(c.ball>=0&&active(c.ball)&&slots_[c.ball]->type==-68){auto& b=*slots_[c.ball]->composite;if(!b.flying){b.fade_velocity=b.velocity;b.dying=true;b.time=0;}}
        if(c.lightning>=0&&active(c.lightning)&&slots_[c.lightning]->type==5001)slots_[c.lightning]->message(0,c.input[25],{});
    }
    c.phase=4;
}
bool Effects::advance_heron(Instance& effect,float frames){
    auto& c=*effect.composite;const auto& p=c.input;effect.age+=frames;
    Instance* ball=c.ball>=0&&active(c.ball)&&slots_[c.ball]->type==-68?&*slots_[c.ball]:nullptr;
    if(c.phase==1){const float t=p[23]!=0?effect.age/p[23]:1;if(ball&&!ball->composite->dying){ball->composite->radius=8-5*t;ball->composite->alpha=t;}
        if(effect.age>=p[23]){c.phase=2;effect.age=0;if(ball)ball->composite->emitting=true;}}
    else if(c.phase==2&&effect.age>=p[24]){
        c.phase=3;effect.age=0;
        if(ball){auto& b=*ball->composite;if(!b.flying&&!b.dying){b.flying=true;const V direction=math::normalize_xyz(math::transform(attachment_(number(p[6]),number(p[7])),vector(p,8)));
            ball->direction=direction*math::dot_xyz(direction,b.velocity);}}
        if(c.lightning>=0&&active(c.lightning)&&slots_[c.lightning]->type==5001)slots_[c.lightning]->message(0,p[25],{});
    }
    // Original phase 3 falls into a persistent, renderless state. Its unused
    // timer is not initialized by 18e948; no emulated heap contents are needed.
    return c.phase!=4;
}
bool Effects::advance_heron_ball(Instance& effect,float frames){
    auto& c=*effect.composite;const auto& p=c.input;auto& trail=effect.particles;
    for(unsigned i=15;i>0;--i){trail[i]=trail[i-1];trail[i].color.w=std::max(0.f,trail[i].color.w-1.f/16);}
    float scale=1;
    if(c.dying){scale=p[9]!=0?1-c.time/p[9]:0;effect.direction=c.fade_velocity*scale;c.time+=frames;if(c.time>p[9])return false;}
    trail[0].size=c.radius*scale;trail[0].color.w=c.alpha*scale;
    if(!c.flying){effect.origin=math::transform(attachment_(number(p[1]),number(p[2])),vector(p,3,1));c.velocity=effect.origin+trail[1].position*(-1);}
    else{
        const V target=attachment_(2,16).columns[3],steering=math::normalize_xyz(target+effect.origin*(-1))*.075f;
        const float speed=length(c.velocity);c.velocity=math::normalize_xyz(math::normalize_xyz(c.velocity)+steering)*speed;effect.origin=effect.origin+c.velocity;
        if(!c.dying){c.time+=frames;if(c.time>p[8]){c.fade_velocity=c.velocity;c.dying=true;c.time=0;}}
        if(number(p[10])&&!c.dying&&surface_contact_){const auto hit=surface_contact_(effect.origin,trail[0].size,number(p[11]),int(std::uint32_t(std::max(0.,std::trunc(double(p[12]))))));
            if(hit){if(number(p[13])>=0&&post_)post_(number(p[13]),number(p[14]),number(p[15]),hit->material);c.fade_velocity=c.velocity;c.dying=true;c.time=0;
                const std::array<float,24> impact{0,0,0,hit->point.x,hit->point.y,hit->point.z,6,.5f,.9f,.95f,1,.7f,.3f,.2f,1,.7f,.4f,.72f,1,.95f,.9f,.2f,1,60};create_damage_wave(impact);}}
    }
    trail[0].position=effect.origin;c.basis=basis(c.velocity,effect.origin);
    if(c.emitting){
        c.trail_time+=frames;if(c.trail_time>=2){c.trail_time-=2;const V d=trail[1].position+effect.origin*(-1);const float radius=trail[0].size;
            const std::array<float,21> ribbon{0,0,0,effect.origin.x,effect.origin.y,effect.origin.z,0,0,d.x,d.y,d.z,radius*.25f,radius,radius*.5f,-radius*1.5f,0,.5f,1,trail[0].color.w,1,8};create_lightning_ribbon(ribbon,texture(5000,"heronthunder3.raw"));}
        create_heron_arc(effect,true);
    }
    effect.age+=frames;if(effect.age>=4){effect.age-=4;create_heron_arc(effect,false);}return true;
}
void Effects::create_heron_arc(const Instance& ball,bool flash){
    const auto unit=[&]{return float(random_()&4095)/4095.f;};const auto& head=ball.particles[0];
    Instance effect;effect.type=flash?-69:-70;effect.composite.emplace();auto& c=*effect.composite;c.basis=ball.composite->basis;
    if(flash)c.radius=(1+unit())*head.size;
    else{c.radius=(.25f+unit()*.75f)*head.size;c.duration=.05f;c.radius_to=(.5f+unit()*.5f)*head.size;c.alpha=head.color.w;if(ball.composite->flying)c.radius_to=length(head.position+ball.particles[1].position*(-1))*4;}
    // Payload random values precede allocation; failed allocations do not consume rotations.
    if(count()==slots_.size())return;
    c.basis=rotate(c.basis,2,unit()*tau);
    if(flash){c.basis=rotate(c.basis,1,unit()*tau);c.basis=rotate(c.basis,0,unit()*tau);}
    effect.lifetime=flash?8:16;effect.textures[0]=texture(5000,flash?"heronthunder5.raw":"heronthunder1.raw");insert(std::move(effect));
}
int Effects::create_damage_wave(std::span<const float> p){
    // 17bb48 / 17c120: six rotating planes and an independent particle burst.
    constexpr float defaults[]{0,0,0,0,40,0,3,.5f,.9f,.95f,1,.7f,.3f,.2f,1,.7f,.4f,.72f,1,.95f,.9f,.2f,1,60};
    if(p.empty())p=defaults;
    if(p.size()!=24)throw std::runtime_error("Damage wave requires 24 parameters");
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Invalid damage wave parameter");
    if(number(p[0])!=0||count()==slots_.size())return -1;
    Instance effect;effect.type=2002;effect.origin=math::transform(attachment_(number(p[1]),number(p[2])),vector(p,3,1));effect.lifetime=p[23];
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    effect.textures={texture(2002,"damagewave_flash.raw"),texture(2002,"damagewave_thunder.raw"),texture(2002,"damagewave_particle.raw")};
    for(unsigned i=0;i<6;++i){Particle point;point.position=effect.origin;point.size=p[6]*(i>=4?.5f:1);point.angle=unit()*tau;point.growth=unit()*std::bit_cast<float>(0x3cd67750u)*(i&1?-1:1);point.life=p[23]*(i>=4?.75f:1);point.color=vector(p,7+4*(i/2),p[10+4*(i/2)]);effect.particles.push_back(point);}
    const V origin=effect.origin;const int slot=insert(std::move(effect));if(slot<0)return -1;
    const float inverse=1/p[23],speed=(1-(1-inverse)/(1+4*inverse))*p[6];
    const std::array<float,21> spray{0,0,0,origin.x,origin.y,origin.z,0,0,0,1,0,tau,0,speed,p[6]*.1f,p[19],p[20],p[21],p[22],0,p[23]};
    const int child=create(17,spray);if(child>=0)slots_[child]->textures[0]=slots_[slot]->textures[2];return slot;
}
std::vector<ModelMesh> Effects::heron_geometry(const Instance& effect,const SceneCamera& camera,std::array<unsigned,3> textures)const{
    std::vector<ModelMesh> output;const V forward=math::normalize_xyz(camera.target+camera.eye*(-1)),right=math::normalize_xyz(math::cross_xyz(camera.up,forward)),up=math::cross_xyz(forward,right);
    const auto quad=[&](std::array<V,4> points,V color,unsigned texture){ModelMesh m;m.texture=texture;m.flags=0x48000000;
        m.vertices={{points[0],{},color,0,0},{points[1],{},color,0,1},{points[2],{},color,1,0},{points[3],{},color,1,1}};m.indices={0,1,2,1,2,3};output.push_back(std::move(m));};
    if(effect.type==-68){for(const auto& point:effect.particles){const V h=right*point.size,v=up*point.size;quad({point.position+h+v,point.position+h+v*(-1),point.position+h*(-1)+v,point.position+h*(-1)+v*(-1)},{.5f,.7f,1,point.color.w},textures[0]);}}
    else if(effect.type==2002){for(unsigned i=0;i<6;++i){const auto& point=effect.particles[i];if(effect.age>point.life)continue;const float t=effect.age/point.life,alpha=(1-t)/(1+4*t),size=(1-alpha)*point.size;
            const float a=float(number(point.angle*.15915493667125702f*65536.f)&65535)*(tau/65536.f);const V h=(right*std::cos(a)+up*std::sin(a))*size,v=(up*std::cos(a)+right*(-std::sin(a)))*size;V color=point.color;color.w*=alpha;
            quad({point.position+h*(-1)+v*(-1),point.position+h*(-1)+v,point.position+h+v*(-1),point.position+h+v},color,textures[i/2]);}}
    else{const auto& c=*effect.composite;const auto point=[&](V local){return math::transform(c.basis,local);};const float alpha=1-effect.age/effect.lifetime;
        if(effect.type==-69){const float size=.75f*c.radius*alpha;quad({point({-size,size,0,1}),point({-size,-size,0,1}),point({size,size,0,1}),point({size,-size,0,1})},{1,1,1,alpha},textures[0]);}
        else for(unsigned axis=0;axis<2;++axis){const float x=axis==0?c.radius*.67f:0,y=axis==1?c.radius*.67f:0,z=c.radius_to*.67f;
            // 1900a8 uses U along the axial dimension and V across the plane.
            ModelMesh m;m.texture=textures[0];m.flags=0x48000000;const V color{.2f,.5f,.95f,c.alpha*alpha};m.vertices={{point({x,y,z,1}),{},color,0,1},{point({x,y,-z,1}),{},color,1,1},{point({-x,-y,z,1}),{},color,0,0},{point({-x,-y,-z,1}),{},color,1,0}};m.indices={0,1,2,1,2,3};output.push_back(std::move(m));}
    }
    return output;
}
}
