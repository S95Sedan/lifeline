#include "game/gaze.hpp"
#include <cmath>
#include <stdexcept>
namespace lifeline {
namespace {
using math::Vector4;using math::Matrix4;
Vector4 unit(Vector4 v){return math::normalize_xyz(v);}
float dot(Vector4 a,Vector4 b){return math::dot_xyz(a,b);}
Vector4 cross(Vector4 a,Vector4 b){return math::cross_xyz(a,b);}
Vector4 rotate(Vector4 value,Vector4 axis,float radians){
    const float c=std::cos(radians),s=std::sin(radians);return value*c+cross(axis,value)*s+axis*(dot(axis,value)*(1-c));
}
bool turn(Vector4& result,Vector4 value,Vector4 from,Vector4 to,float factor){
    auto axis=cross(from,to);const float length=std::sqrt(dot(axis,axis));
    // 1de050 deliberately leaves parallel and antiparallel directions alone.
    if(length<.01f){result=value;return false;}
    result=rotate(value,axis*(1/length),std::acos(std::clamp(dot(from,to),-1.f,1.f))*factor);return true;
}
Matrix4 inverse_rigid(const Matrix4& m){
    Matrix4 r=math::identity();
    r.columns[0]={m.columns[0].x,m.columns[1].x,m.columns[2].x,0};
    r.columns[1]={m.columns[0].y,m.columns[1].y,m.columns[2].y,0};
    r.columns[2]={m.columns[0].z,m.columns[1].z,m.columns[2].z,0};
    r.columns[3]=math::transform(r,m.columns[3]*-1);r.columns[3].w=1;return r;
}
struct Limits {float left,right,down,up,left_weight,right_weight,down_weight,up_weight;};
Vector4 constrain(Vector4 direction,const Matrix4& base,const Limits& limits){
    const auto x=unit(base.columns[0]),y=unit(base.columns[1]),z=unit(base.columns[2]);
    auto horizontal=unit(direction+y*(-dot(direction,y)));
    auto vertical=unit(direction+x*(-dot(direction,x)));
    const bool left=dot(horizontal,x)<0;
    const float hw=left?limits.left_weight:limits.right_weight;
    if(hw<1){
        const float angle=std::acos(std::clamp(dot(horizontal,z),-1.f,1.f));
        direction=rotate(direction,y,(left?angle:-angle)*hw);
        horizontal=unit(direction+y*(-dot(direction,y)));vertical=unit(direction+x*(-dot(direction,x)));
    }
    const bool down=dot(vertical,y)<0;const float vw=down?limits.down_weight:limits.up_weight;
    if(vw<1){
        const float angle=std::acos(std::clamp(dot(vertical,z),-1.f,1.f));
        direction=rotate(direction,x,(down?-angle:angle)*vw);
        horizontal=unit(direction+y*(-dot(direction,y)));vertical=unit(direction+x*(-dot(direction,x)));
    }
    if(dot(direction,z)<0)return unit(z+x*(dot(horizontal,x)<0?limits.left:limits.right)+y*(dot(vertical,y)<0?limits.down:limits.up));
    const float h=dot(horizontal,x),v=dot(vertical,y);
    if(h<limits.left)horizontal=unit(z+x*limits.left);else if(h>limits.right)horizontal=unit(z+x*limits.right);
    if(v<limits.down)vertical=unit(z+y*limits.down);else if(v>limits.up)vertical=unit(z+y*limits.up);
    Vector4 result;turn(result,vertical,z,horizontal,1);return result;
}
Matrix4 facing(const Matrix4& base,Vector4 forward){
    Matrix4 result=base;result.columns[2]=forward;result.columns[2].w=0;
    auto x=cross(base.columns[1],forward);
    if(dot(x,x)>0){result.columns[0]=unit(x);result.columns[1]=cross(forward,result.columns[0]);}
    else {result.columns[0]=base.columns[0];result.columns[1]=unit(cross(forward,result.columns[0]));result.columns[0]=cross(result.columns[1],forward);}
    result.columns[0].w=result.columns[1].w=0;return result;
}
Vector4 local_direction(const Matrix4& basis,Vector4 value){
    return {dot(basis.columns[0],value),dot(basis.columns[1],value),dot(basis.columns[2],value),0};
}
void leg(const Skeleton& skeleton,std::span<Matrix4> pose,const std::vector<Matrix4>& original,unsigned index,float weight){
    // 1de6a0: preserve the ankle anchor and solve the thigh/calf triangle.
    auto& hip=pose[index];auto& knee=pose[index+1];auto& ankle=pose[index+2];
    const auto ankle_local=math::multiply(inverse_rigid(original[index+1]),original[index+2]);
    const auto toe_local=math::multiply(inverse_rigid(original[index+2]),original[index+3]);
    hip.columns[3]=math::transform(pose[1],skeleton.bones[index].translation);
    const auto span=hip.columns[3]+ankle.columns[3]*-1;
    hip.columns[1]=unit(span);hip.columns[2]=unit(cross(hip.columns[0],hip.columns[1]));hip.columns[0]=cross(hip.columns[1],hip.columns[2]);
    const float upper=std::sqrt(dot(skeleton.bones[index+1].translation,skeleton.bones[index+1].translation));
    const float lower=std::sqrt(dot(skeleton.bones[index+2].translation,skeleton.bones[index+2].translation));
    const float distance=dot(hip.columns[1],span);
    float y=distance==0?-upper:-(distance*distance+upper*upper-lower*lower)/(2*distance);
    float square=upper*upper-y*y;
    if(square<=0){y=-upper;square=0;}
    const auto bend=math::transform(hip,{0,y,std::sqrt(square),1});
    hip.columns[0]=hip.columns[0]*(1-weight*.5f)+pose[1].columns[0]*(weight*.5f);
    hip.columns[1]=unit(hip.columns[3]+bend*-1);
    hip.columns[2]=unit(cross(hip.columns[0],hip.columns[1]));hip.columns[0]=cross(hip.columns[1],hip.columns[2]);
    knee.columns[3]=math::transform(hip,skeleton.bones[index+1].translation);
    knee.columns[0]=hip.columns[0];knee.columns[1]=unit(knee.columns[3]+ankle.columns[3]*-1);
    knee.columns[2]=unit(cross(knee.columns[0],knee.columns[1]));knee.columns[0]=cross(knee.columns[1],knee.columns[2]);
    const auto anchor=ankle.columns[3];ankle=math::multiply(knee,ankle_local);ankle.columns[3]=anchor;
    pose[index+3]=math::multiply(ankle,toe_local);
}
void body(const Skeleton& skeleton,std::span<Matrix4> pose,const std::vector<Matrix4>& original,
          int mode,Vector4 target,bool relative,float speed,Vector4& retained_target,Vector4& retained_direction){
    // 1de250: stance-relative pelvis lean, then two anchored leg solves.
    const float weight=skeleton.bones[1].gaze_weight;
    if(!std::isfinite(weight) || weight<0 || weight>1)throw std::runtime_error("Invalid body compensation weight");
    auto& pelvis=pose[1];
    Matrix4 reference=pelvis;
    const auto stance=unit(pose[2].columns[3]+pose[6].columns[3]*-1);
    reference.columns[0]=pelvis.columns[0]*(1-weight*.5f)+stance*(weight*.5f);
    reference.columns[2]=unit(cross(reference.columns[0],reference.columns[1]));
    reference.columns[0]=cross(reference.columns[1],reference.columns[2]);
    if(mode<2){
        const auto origin=relative?Vector4{}:pelvis.columns[3];
        const auto prior=unit(retained_target+origin*-1),next=unit(target+origin*-1);
        auto direction=prior*(16*(1-speed))+next*(16*speed);
        retained_target=direction+origin;
        retained_direction=local_direction(reference,direction);
        retained_direction.x*=.33f;retained_direction.y*=retained_direction.y>0?.1f:.2f;
    }
    // The original loader sets this mix to zero; no deformation is needed
    // until its runtime value enables compensation. Still update target memory.
    if(weight==0)return;
    auto direction=unit(retained_direction);
    direction.x=std::clamp(direction.x,-1.f/3,1.f/3);direction.y=std::clamp(direction.y,-1.f/3,.1f);
    direction.z=std::sqrt(std::max(0.f,1-direction.x*direction.x-direction.y*direction.y));direction.w=0;
    const auto world=math::transform(reference,direction);
    pelvis.columns[2]=unit(pelvis.columns[2]*(1-weight)+world*weight);
    pelvis.columns[0]=unit(cross(pelvis.columns[1],pelvis.columns[2]));pelvis.columns[1]=cross(pelvis.columns[2],pelvis.columns[0]);
    const float bend=direction.y*weight*(direction.y>0?1.f:3.f);
    Vector4 offset{world.x*bend,0,world.z*bend,0};offset.y=-std::sqrt(offset.x*offset.x+offset.z*offset.z)*.5f;
    pelvis.columns[3]=pelvis.columns[3]+offset;
    leg(skeleton,pose,original,6,weight);leg(skeleton,pose,original,2,weight);
}
}
void Gaze::speed(float value){if(!std::isfinite(value) || value<0)throw std::runtime_error("Invalid gaze speed");speed_=value;}
void Gaze::target(Vector4 value,bool direction){
    if(!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))throw std::runtime_error("Invalid gaze target");
    target_=direction?unit(value):value;relative_=direction;
}
void Gaze::modes(int eyes,int head,int body){
    // 1ded20 stores the low two bits; negative arguments retain each group.
    const auto group=[&](int value,unsigned first,unsigned bit){if(value<0)return;
        value&=3;joints_[first].mode=joints_[first+1].mode=value;
        requested_modes_=(requested_modes_&~bit)|(value?bit:0);
    };
    group(eyes,4,1);group(head,2,2);group(body,0,4);if(body>=0)body_mode_=body&3;
}
unsigned Gaze::modes()const{return requested_modes_;}
void Gaze::apply(const Skeleton& skeleton,std::span<Matrix4> pose){
    if(!skeleton.facial)return;
    if(pose.size()!=skeleton.bones.size() || pose.size()<64)throw std::runtime_error("Gaze requires the actor head/eye bones");
    constexpr std::array<unsigned,6> bones{10,11,12,39,62,63};
    constexpr std::array<Limits,6> limits{{{-.6f,.6f,-.6f,.1f,1,1,1,.1f},{-.6f,.6f,-.6f,.1f,1,1,1,.1f},{-.2f,.2f,-.2f,.25f,1,1,1,.25f},{-.5f,.5f,-.2f,.5f,1,1,.5f,1},{-.3f,.5f,-.25f,.1f,.5f,.5f,.33f,.33f},{-.5f,.3f,-.25f,.1f,.5f,.5f,.33f,.33f}}};
    const std::vector<Matrix4> original(pose.begin(),pose.end());
    for(unsigned i=0;i<pose.size();++i){
        const int parent=skeleton.bones[i].parent;
        if(parent>=0)pose[i]=math::multiply(pose[unsigned(parent)],math::multiply(inverse_rigid(original[unsigned(parent)]),original[i]));
        if(i==9 && body_mode_>=0)body(skeleton,pose,original,body_mode_,target_,relative_,speed_,body_target_,body_direction_);
        const auto at=std::find(bones.begin(),bones.end(),i);if(at==bones.end())continue;
        const auto index=unsigned(at-bones.begin());auto& joint=joints_[index];
        if(i==10 && joint.mode>=0){
            auto offset=skeleton.bones[10].translation+skeleton.bones[1].translation*-1;offset.w=1;
            pose[i].columns[3]=math::transform(pose[1],offset);
        }
        const auto base=pose[i];
        if(!joint.initialized || joint.mode<0){joint.direction=unit(base.columns[2]);joint.initialized=true;if(joint.mode<0)continue;}
        if(joint.mode==3)continue; // The original general aim helper has no case 3.
        Vector4 desired=unit(base.columns[2]);
        if(joint.mode>0){
            if(joint.mode==1)joint.target=relative_?base.columns[3]+target_:target_;
            auto d=unit(joint.target+base.columns[3]*-1);
            // 1dd754 copies the parent basis, then replaces its origin with
            // this joint's position. Both the reference-axis correction and
            // angular limits use that basis, not the animated joint basis.
            auto reference=parent>=0?pose[unsigned(parent)]:base;
            reference.columns[3]=base.columns[3];
            const auto axis=unit(math::transform(reference,skeleton.bones[i].gaze_axis));
            turn(d,d,axis,unit(reference.columns[2]),1);
            desired=constrain(d,reference,limits[index]);
        }
        Vector4 direction;
        const bool turning=turn(direction,joint.direction,joint.direction,desired,index>=4?.25f:speed_);
        joint.direction=direction;if(joint.mode==0 && !turning)joint.mode=-1;
        pose[i]=facing(base,joint.direction);
    }
}
}
