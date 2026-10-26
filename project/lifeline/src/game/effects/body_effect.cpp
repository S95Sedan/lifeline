#include "game/effects/effects.hpp"
#include <cmath>
#include <numbers>

namespace lifeline::script {
namespace {
// Recovered from 175794..175e68, 1761d4..176484 and 305f98.
constexpr std::array<std::array<float,14>,9> enemy_rings{{
    {{1.f,4.f,10.f,90.f,-10.f,0.f,0.32100001f,0.f,-0.0130000003f,3.f,0.970000029f,1.47000003f,0.600000024f,15.f}},
    {{1.f,4.f,11.f,90.f,10.f,0.f,0.32100001f,0.f,-0.0130000003f,3.f,0.970000029f,1.47000003f,0.600000024f,15.f}},
    {{1.f,4.f,12.f,110.f,0.f,0.f,0.f,-0.495999992f,1.03499997f,3.01999998f,3.30699992f,3.80699992f,0.600000024f,15.f}},
    {{1.f,4.f,13.f,100.f,0.f,0.f,0.f,-2.25600004f,-1.59300005f,3.45000005f,4.16099977f,4.66099977f,0.600000024f,15.f}},
    {{1.f,4.f,14.f,90.f,0.f,0.f,0.f,-1.f,-1.50600004f,4.5999999f,3.81999993f,4.32000017f,0.600000024f,15.f}},
    {{1.f,4.f,15.f,80.f,0.f,0.f,0.f,-3.11599994f,-1.60000002f,4.f,3.69000006f,4.19000006f,0.600000024f,15.f}},
    {{1.f,4.f,16.f,70.f,0.f,0.f,0.f,-1.15600002f,-0.704999983f,3.f,3.29999995f,3.79999995f,0.600000024f,15.f}},
    {{1.f,4.f,17.f,109.629997f,0.f,0.f,0.f,0.397000015f,-0.884000003f,2.5f,2.29999995f,2.79999995f,0.600000024f,15.f}},
    {{1.f,4.f,18.f,120.f,0.f,0.f,0.f,2.92499995f,-4.17000008f,4.5f,1.20000005f,1.70000005f,0.600000024f,15.f}},
}};
constexpr std::array<std::array<float,14>,3> sparse_rings{{
    {{1.f,4.f,8.f,90.f,0.f,0.f,0.0989999995f,0.474000007f,-0.921000004f,1.79999995f,0.800000012f,1.29999995f,0.400000006f,4.f}},
    {{1.f,4.f,7.f,90.f,35.f,0.f,-0.757000029f,-0.162f,-0.744000018f,3.29999995f,1.53999996f,2.03999996f,0.400000006f,4.f}},
    {{1.f,4.f,9.f,90.f,0.f,0.f,0.0260000005f,0.453999996f,-0.328999996f,2.f,1.26999998f,1.76999998f,0.400000006f,4.f}},
}};
constexpr std::array<std::array<float,14>,12> actor_rings{{
    {{1.f,2.f,10.f,0.f,0.f,0.f,0.f,0.f,0.f,5.f,0.800000012f,1.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,11.f,0.f,0.f,0.f,0.f,0.f,0.f,5.f,0.800000012f,1.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,8.f,0.f,0.f,0.f,0.f,0.f,0.f,5.f,0.800000012f,1.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,9.f,0.f,0.f,0.f,0.f,0.f,0.f,5.f,0.800000012f,1.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,14.f,0.f,0.f,0.f,0.f,0.f,0.f,3.f,1.70000005f,2.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,15.f,0.f,0.f,0.f,0.f,0.f,0.f,1.5f,1.29999995f,1.70000005f,0.400000006f,15.f}},
    {{1.f,2.f,16.f,0.f,0.f,0.f,0.f,0.f,0.f,2.f,1.5f,2.f,0.400000006f,15.f}},
    {{1.f,2.f,3.f,0.f,0.f,0.f,0.f,0.f,0.f,2.f,0.800000012f,1.29999995f,0.400000006f,15.f}},
    {{1.f,2.f,6.f,0.f,0.f,0.f,0.f,0.f,0.f,3.f,0.5f,1.f,0.400000006f,15.f}},
    {{1.f,2.f,7.f,0.f,0.f,0.f,0.f,0.f,0.f,3.f,0.5f,1.f,0.400000006f,15.f}},
    {{1.f,2.f,4.f,0.f,0.f,0.f,0.f,0.f,0.f,3.f,0.5f,1.f,0.400000006f,15.f}},
    {{1.f,2.f,5.f,0.f,0.f,0.f,0.f,0.f,0.f,3.f,0.5f,1.f,0.400000006f,15.f}},
}};
constexpr float pi=std::numbers::pi_v<float>;
float turn(float angle){return float(int(angle/(2*pi)*65536.f)&65535)*(2*pi/65536.f);}
math::Vector4 rotate(math::Vector4 value,math::Vector4 axis,float angle){
    axis=math::normalize_xyz(axis);angle=turn(angle);
    return value*std::cos(angle)+math::cross_xyz(axis,value)*std::sin(angle)+axis*(math::dot_xyz(axis,value)*(1-std::cos(angle)));
}
math::Matrix4 rotation(unsigned axis,float angle){
    auto result=math::identity();const auto direction=result.columns[axis];
    for(unsigned i=0;i<3;++i)result.columns[i]=rotate(result.columns[i],direction,angle);
    return result;
}
void strip_indices(ModelMesh& mesh){for(unsigned i=2;i<mesh.vertices.size();++i){mesh.indices.push_back(i-2);mesh.indices.push_back(i-1);mesh.indices.push_back(i);}}
}
int Effects::create_body_effect(std::span<const float> input){
    if(count()==slots_.size())return -1;
    if(!input.empty() && input.size()!=3 && input.size()!=14)throw std::runtime_error("Invalid body effect payload extent");
    Instance effect;effect.type=3003;effect.parameters[2]=1;
    for(float v:input)if(!std::isfinite(v))throw std::runtime_error("Non-finite body effect parameter");
    if(!input.empty())std::copy(input.begin(),input.end(),effect.parameters.begin());
    const auto& p=effect.parameters;
    if(p[0]<-2147483648.f || double(p[0])>=2147483648. || p[1]<-2147483648.f || double(p[1])>=2147483648. || p[2]<-2147483648.f || double(p[2])>=2147483648.)throw std::runtime_error("Invalid body effect selector");
    if(!input.empty() && input.size()!=(int(std::nearbyint(p[0]))==3?14:3))throw std::runtime_error("Body effect payload does not match mode");
    effect.textures[0]=texture(3003,"cat_tpipe.raw");
    if(!effect.textures[0])throw std::runtime_error("Body effect texture is not installed or loaded: cat_tpipe.raw");
    return insert(std::move(effect));
}
void Effects::emit_body_ring(std::span<const float> p,const std::shared_ptr<const AssetImage>& texture){
    if(count()==slots_.size())return;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    const float spin=unit()*6.28f-3.14f;
    const float u=float(random_()&3)*.25f;
    const auto parent=attachment_(int(std::nearbyint(p[1])),int(std::nearbyint(p[2])));
    const auto basis=math::multiply(parent,math::multiply(rotation(2,p[5]*pi/180),math::multiply(rotation(1,p[4]*pi/180),rotation(0,p[3]*pi/180))));
    auto origin=math::transform(parent,{p[6],p[7],p[8],1});
    origin=origin+basis.columns[1]*(unit()*p[9]);origin.w=1;
    const float radius=p[10]+(p[11]-p[10])*unit();
    const auto x=rotate(basis.columns[0],basis.columns[1],spin),z=rotate(basis.columns[2],basis.columns[1],spin),y=basis.columns[1];
    Instance child;child.type=-60;child.lifetime=p[13];child.textures[0]=texture;child.authored_mesh.emplace();
    auto& mesh=*child.authored_mesh;mesh.flags=0x48000000;
    // 177c98..177dd0: six-sided open tube, alternating the two ends.
    for(unsigned i=0;i<=6;++i){
        const float angle=float(int(float(i)/6*65536)&65535)*(2*pi/65536);
        const auto radial=(x*std::sin(angle)+z*std::cos(angle))*radius;
        for(unsigned end=0;end<2;++end){auto position=origin+radial+y*(end?p[12]:-p[12]);position.w=1;
            mesh.vertices.push_back({position,{0,0,0,0},{1,1,1,1},u+end*.25f,float(i)/3});}
    }
    strip_indices(mesh);insert(std::move(child));
}
void Effects::emit_body_strip(int source,int part,bool small,const std::shared_ptr<const AssetImage>& texture){
    if(count()==slots_.size())return;
    const auto unit=[&]{return float(random_()&4095)/4095.f;};
    const auto origin=math::transform(attachment_(source,part),{0,0,0,1});
    const float x=unit()*(2*pi)-pi,unused_y=unit()*(2*pi)-pi,z=unit()*(2*pi)-pi;
    (void)unused_y; // Original 178508 reads +28 again for its final rotation.
    const float radius=(small?1.f:3.f)+unit()*(small?1.f:2.f);
    const float u=float(random_()&3)*.25f;
    const auto basis=math::multiply(rotation(1,z),math::multiply(rotation(0,x),rotation(2,z)));
    Instance child;child.type=-61;child.lifetime=15;child.textures[0]=texture;child.authored_mesh.emplace();
    auto& mesh=*child.authored_mesh;mesh.flags=0x48000000;
    const std::array<math::Vector4,4> points{{{radius,0,-.5f,1},{0,0,-.5f,1},{radius,0,.5f,1},{0,0,.5f,1}}};
    for(unsigned i=0;i<4;++i){auto position=origin+math::transform(basis,points[i]);position.w=1;mesh.vertices.push_back({position,{0,0,0,0},{1,1,1,1},u+(i/2)*.25f,float(i%2)});}
    strip_indices(mesh);insert(std::move(child));
}
void Effects::emit_body_effect(Instance& effect){
    const auto p=effect.parameters;const int mode=int(std::nearbyint(p[0]));const auto bitmap=effect.textures[0];
    if(mode==1 || mode==2){
        const unsigned count=mode==1?9:3;
        unsigned selected=effect.aura_step%count;
        // 176510: sparse mode samples thirty choices; only 0..2 emit.
        if(mode==2)selected=unsigned(std::nearbyint(float(random_()&4095)/4095.f*30));
        if(selected<count){
            auto record=mode==1?enemy_rings[selected]:sparse_rings[selected];record[1]=float(int(std::nearbyint(p[1])))+3;
            for(unsigned i=9;i<=12;++i)record[i]*=p[2];
            emit_body_ring(record,bitmap);emit_body_strip(int(record[1]),int(record[2]),mode==2,bitmap);
        }
        effect.aura_step=(effect.aura_step+1)%count;
    }else if(mode==3){
        std::array<float,14> record;std::copy_n(p.begin(),14,record.begin());record[0]=1;
        emit_body_ring(record,bitmap);emit_body_strip(int(std::nearbyint(p[1])),int(std::nearbyint(p[2])),true,bitmap);
    }else{
        for(unsigned offset:{0u,4u,8u})emit_body_ring(actor_rings[(effect.aura_step+offset)%12],bitmap);
        const auto& record=actor_rings[effect.aura_step%12];emit_body_strip(int(record[1]),int(record[2]),false,bitmap);
        effect.aura_step=(effect.aura_step+1)%12;
    }
}
// 178940 / 178b70: attached light samples and their independent fading trail.
void Effects::emit_trail(Instance& emitter){
    if(!viewpoint_)throw std::runtime_error("Effect camera position is not connected");
    auto& p=emitter.parameters;emitter.trail[2]=emitter.trail[1];emitter.trail[1]=emitter.trail[0];
    emitter.jitter[2]=emitter.jitter[1];emitter.jitter[1]=emitter.jitter[0];
    const auto matrix=attachment_(int(std::nearbyint(p[0])),int(std::nearbyint(p[1])));
    const auto point=math::transform(matrix,{p[2],p[3],p[4],1});emitter.trail[0]=point;
    const auto direction=math::normalize_xyz(math::transform(matrix,{p[5],p[6],p[7],0}));
    const auto toward=math::normalize_xyz(viewpoint_()+point*(-1));
    emitter.jitter[0]=std::max(0.f,math::dot_xyz(toward,direction))*p[12];
    Instance light;light.type=-62;light.origin=point;light.lifetime=8;
    light.radial_axis={p[8],p[9],p[10],p[11]};light.parameters[0]=emitter.jitter[0];insert(std::move(light));
    if(emitter.jitter[0]>0){
        Instance trail;trail.type=-63;trail.trail=emitter.trail;trail.lifetime=16;
        trail.radial_axis={p[8],p[9],p[10],p[11]};trail.parameters[0]=emitter.jitter[0];trail.parameters[1]=emitter.jitter[1];insert(std::move(trail));
    }
}
}
