#include "game/effects/effects.hpp"
#include <algorithm>
#include <numbers>

namespace lifeline::script {
namespace {
int selector(float x){
    if(!std::isfinite(x)||double(x)<-2147483648.||double(x)>=2147483648.)throw std::runtime_error("Invalid explosion selector");
    return int(x);
}
}
// 17e468: the parent reserves its slot before creating independent children.
int Effects::create_explosion(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty()&&input.size()!=13)throw std::runtime_error("Explosion requires 13 parameters");
    std::array<float,13> p{0,0,0,0,0,0,0,0,0,1,0,1,10};
    if(!input.empty()){
        const int mode=selector(input[0]);
        if(mode==0)std::copy(input.begin(),input.end(),p.begin());
        else if(mode==1)std::copy_n(input.begin()+1,5,p.begin()+1);
    }
    for(float x:p)if(!std::isfinite(x))throw std::runtime_error("Nonfinite explosion parameter");
    // The child builders deliberately use the source selector twice, as at
    // 17e61c/17e628 and 17e6d0/17e6dc; the parent's part selectors are unused.
    p[1]=float(selector(p[1]));p[6]=float(selector(p[6]));
    Instance parent;parent.type=9;const int handle=insert(std::move(parent));
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    const auto smoke_parameters=[&]{
        std::array<float,32> q{};q[1]=q[2]=p[1];q[6]=q[7]=p[6];
        std::copy_n(p.begin()+3,3,q.begin()+3);std::copy_n(p.begin()+8,3,q.begin()+8);return q;
    };
    for(unsigned i=0;i<4;++i){
        const float speed=unit()*.15f*p[11];auto q=smoke_parameters();
        for(unsigned k=3;k<6;++k)q[k]+=unit()*7*p[11];
        for(unsigned k:{13u,14u,15u,17u,18u,19u,21u,22u,23u})q[k]=.7f;
        q[16]=q[20]=1;q[25]=q[26]=speed;q[28]=6*p[11];q[29]=8*p[11];q[30]=80;q[31]=60;
        create(4,q);
    }
    for(unsigned i=0;i<4;++i){
        std::array<float,14> q{};q[0]=q[1]=p[1];q[5]=q[6]=p[6];
        std::copy_n(p.begin()+3,3,q.begin()+2);std::copy_n(p.begin()+8,3,q.begin()+7);
        if(i)for(unsigned k=2;k<5;++k)q[k]+=unit()*7*p[11];
        q[10]=1;q[11]=unit()*.4f+.8f;q[12]=p[11];q[13]=(unit()-.5f)*40+40;
        create_explosion_sprite(q,false);
    }
    for(unsigned i=0;i<6;++i){
        const int duration=selector((unit()-.5f)*40+40);auto q=smoke_parameters();
        if(i)for(unsigned k=3;k<6;++k)q[k]+=unit()*7*p[11];
        for(unsigned k:{11u,12u,13u,14u,15u,17u,18u,19u,20u,21u,22u,23u})q[k]=1;
        q[28]=6*p[11];q[29]=12*p[11];q[30]=float(duration);q[31]=float(duration/2);
        const int child=create(4,q);
        if(child>=0){slots_[child]->explosion_child=Instance::ExplosionChild::fire;slots_[child]->textures[0]=texture(9,"fire.raw");}
    }
    for(unsigned i=0;i<8;++i){
        std::array<float,16> q{};q[0]=q[1]=p[1];q[5]=q[6]=p[6];
        for(unsigned k=0;k<3;++k)q[k+2]=p[k+3]+unit()*5*p[11];
        std::copy_n(p.begin()+8,3,q.begin()+7);
        q[11]=unit()*.2f+.2f;q[12]=(unit()-.5f)*.6f;q[13]=(unit()-.5f)*.6f;
        q[14]=p[11];q[15]=(unit()-.5f)*40+40;create_explosion_sprite(q,true);
    }
    return handle;
}
void Effects::create_explosion_sprite(std::span<const float> p,bool spark){
    if(count()==slots_.size())return;
    Instance child;child.type=9;child.explosion_child=spark?Instance::ExplosionChild::spark:Instance::ExplosionChild::bomb;
    std::copy(p.begin(),p.end(),child.parameters.begin());auto& q=child.parameters;
    child.origin=math::transform(attachment_(selector(p[0]),selector(p[1])),{p[2],p[3],p[4],1});
    child.direction=math::transform(attachment_(selector(p[5]),selector(p[6])),{p[7],p[8],p[9],0});
    const float scale=p[spark?14:12];q[32]=p[10]*scale;q[33]=p[11]*scale;
    child.lifetime=p[spark?15:13];q[38]=float(random_()&4095)/4095.f-3.14f;
    if(spark){q[36]=p[12]*scale;q[37]=p[13]*scale;q[39]=float(random_()&4095)/4095.f<.5f?1.f:-1.f;}
    child.textures[0]=texture(9,spark?"spark1.raw":"bomb1.raw");insert(std::move(child));
}
// 182d00 prepares all three parameter blocks before allocating any children.
int Effects::create_burst(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty()&&input.size()!=12)throw std::runtime_error("Burst requires 12 parameters");
    std::array<float,12> p{0,0,0,0,0,0,0,0,0,1,0,1};
    if(!input.empty()){
        const int mode=selector(input[0]);if(mode==0)std::copy(input.begin(),input.end(),p.begin());
        else if(mode==1)std::copy_n(input.begin()+1,5,p.begin()+1);
    }
    for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Nonfinite burst parameter");
    Instance parent;parent.type=10003;const int handle=insert(std::move(parent));
    std::array<std::array<float,32>,3> children{};
    for(unsigned i=0;i<3;++i){
        auto& q=children[i];std::copy_n(p.begin()+1,7,q.begin()+1);
        for(unsigned k=8;k<11;++k)q[k]=p[k]+(float(random_()&4095)/4095.f-.5f)*.2f;
        q[12]=i==2?0.f:1.f;
        for(unsigned k:{13u,14u,15u,17u,18u,19u,21u,22u,23u})q[k]=i==2?.7f:.8f;
        q[20]=i==0?.3f:i==1?.4f:1.f;
        q[25]=p[11]*(i==0?.04f:i==1?.1f:.2f);
        q[26]=p[11]*(i==0?.08f:i==1?.15f:.2f);
        q[27]=p[11]*(i==0?12.f:i==1?9.6f:3.f);
        q[28]=p[11]*(i==0?8.f:i==1?5.76f:5.8f);
        q[29]=p[11]*(i==0?4.8f:i==1?4.f:10.f);
        q[30]=i==1?60.f:70.f;q[31]=25;
    }
    for(unsigned i=0;i<3;++i){const int slot=create(4,children[i]);if(slot<0)continue;
        if(i){slots_[slot]->explosion_child=i==1?Instance::ExplosionChild::star:Instance::ExplosionChild::fire;
            slots_[slot]->textures[0]=texture(10003,i==1?"spark.raw":"fire.raw");}
    }
    return handle;
}
ModelMesh Effects::explosion_geometry(const Instance& child,const SceneCamera& camera,unsigned texture,float turn)const{
    const auto& p=child.parameters;const bool fire=child.explosion_child==Instance::ExplosionChild::fire||child.explosion_child==Instance::ExplosionChild::star;
    const bool spark=child.explosion_child==Instance::ExplosionChild::spark;
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));
    const auto x=math::normalize_xyz(math::cross_xyz(camera.up,forward));const auto y=math::cross_xyz(forward,x);
    const float t=child.age/child.lifetime;
    const float radians=fire?p[39]+turn*t*1.047197699546814f:p[38]+t*.39269912242889404f*(spark?p[39]:1.f);
    const float angle=float(selector(radians*.15915493667125702f*65536.f)&65535)*(2*std::numbers::pi_v<float>/65536.f);
    const float size=fire?p[38]:p[32];
    const auto h=(x*std::cos(angle)+y*std::sin(angle))*size,v=(y*std::cos(angle)+x*(-std::sin(angle)))*size;
    const auto origin=child.origin+(spark?x*p[35]+y*p[34]:math::Vector4{});
    ModelMesh mesh;mesh.texture=texture;mesh.flags=fire?(p[12]==1?0x48000000:p[12]==2?0x42000000:0x44000000):0x48000000;
    if(fire){
        const auto color=child.radial_axis;auto edge=color;edge.w=0;mesh.vertices.push_back({origin,{},color,.5f,.5f});
        for(unsigned i=0;i<=8;++i){const float a=float(i)*std::numbers::pi_v<float>/4,c=std::cos(a),s=std::sin(a);
            mesh.vertices.push_back({origin+h*c+v*s,{},edge,.5f+s*.5f,.5f+c*.5f});if(i)mesh.indices.insert(mesh.indices.end(),{0,i,i+1});}
    }else{
        const math::Vector4 color{1,1,1,1-t};
        mesh.vertices={{{origin+h*(-1)+v*(-1)}, {},color,0,0},{{origin+h+v*(-1)}, {},color,1,0},{{origin+h*(-1)+v}, {},color,0,1},{{origin+h+v}, {},color,1,1}};
        mesh.indices={0,1,2,1,2,3};
    }
    return mesh;
}
}
