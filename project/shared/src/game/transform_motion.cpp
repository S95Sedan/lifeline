#include "game/transform_motion.hpp"
#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace lifeline {
namespace {
void finite(math::Vector4 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Non-finite motion vector");}
float& component(math::Vector4& v,unsigned axis){switch(axis){case 0:return v.x;case 1:return v.y;case 2:return v.z;default:throw std::out_of_range("Rotation axis outside 0..2");}}
}
float TransformMotion::angle(float value){
    if(!std::isfinite(value))throw std::runtime_error("Non-finite rotation angle");
    constexpr float pi=std::numbers::pi_v<float>,tau=2*pi;
    // Preserve the original (-pi,pi] interval and single-precision steps.
    if(std::abs(value)>4096*tau)value=std::fmod(value,tau);
    while(value>pi)value-=tau;
    while(value<=-pi)value+=tau;
    return value;
}
void TransformMotion::move_to(math::Vector4 target,float speed){
    finite(position);finite(target);
    if(!std::isfinite(speed)||speed<=0)throw std::runtime_error("Translation speed must be positive");
    const math::Vector4 delta{target.x-position.x,target.y-position.y,target.z-position.z,0};
    const float distance=std::sqrt(math::dot_xyz(delta,delta));
    // Equal endpoints leave the existing controller untouched (1d8adc).
    if(distance==0)return;
    const float duration=distance/speed;
    if(!std::isfinite(duration)||double(duration)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Translation duration exceeds native range");
    origin_=position;target_=target;target_.w=1;velocity_=delta*(speed/distance);
    elapsed_=0;duration_=std::int32_t(duration);moving_=true;
}
void TransformMotion::trajectory(math::Vector4 velocity,math::Vector4 acceleration){
    finite(position);finite(velocity);finite(acceleration);
    origin_=position;velocity_=velocity;acceleration_=acceleration;
    velocity_.w=acceleration_.w=0;elapsed_=0;duration_=-1;moving_=true;
}
void TransformMotion::turn(unsigned axis,float target,float maximum_step){
    if(!std::isfinite(target)||!std::isfinite(maximum_step)||maximum_step<0)throw std::runtime_error("Invalid angular motion");
    auto& t=turns_.at(axis);t.target=target;t.limit=maximum_step;t.active=true;
}
bool TransformMotion::velocity_near_zero()const{
    // 1d9a18 compares |initial velocity + acceleration * elapsed| to |acceleration|.
    const auto velocity=velocity_+acceleration_*float(elapsed_);
    return std::sqrt(math::dot_xyz(velocity,velocity))<=std::sqrt(math::dot_xyz(acceleration_,acceleration_));
}
void TransformMotion::stop_turn(unsigned axis){auto& t=turns_.at(axis);t.active=false;t.velocity=0;}
void TransformMotion::turn_gain(float value){
    if(!std::isfinite(value))throw std::runtime_error("Non-finite angular gain");
    turn_gain_=value;for(auto& turn:turns_)turn.velocity=0;
}
math::Vector4 TransformMotion::next_position()const{
    if(!moving_)return position;
    const auto next=std::int64_t(elapsed_)+1;
    if(duration_>=0 && next>=duration_)return target_;
    auto result=origin_+velocity_*float(next);
    if(duration_<0)result=result+(acceleration_*.5f)*float(next*next);
    result.w=1;return result;
}
void TransformMotion::advance(){
    for(unsigned axis=0;axis<turns_.size();++axis){
        auto& t=turns_[axis];if(!t.active)continue;
        auto& value=component(rotation,axis);
        float step=std::clamp(angle(t.target-value)*turn_gain_,-t.limit,t.limit);
        // 1d9214/1d9288 apply the previous velocity this tick and advance
        // the retained velocity by 2% of the limit for the following tick.
        // A retarget can therefore continue briefly in the old direction.
        if(turn_acceleration_ && step>0 && t.velocity<step){step=t.velocity;t.velocity=step+t.limit*.02f;}
        else if(turn_acceleration_ && step<0 && step<t.velocity){step=t.velocity;t.velocity=step-t.limit*.02f;}
        else t.velocity=step;
        value=angle(value+step);
        // Original compares raw target difference, not wrapped difference.
        if(std::abs(double(value-t.target))<double(.001f))stop_turn(axis);
    }
    if(moving_){
        position=next_position();
        if(elapsed_==std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Motion tick counter overflow");
        ++elapsed_;if(duration_>=0 && elapsed_>=duration_)moving_=false;
    }
}
math::Matrix4 TransformMotion::matrix()const{
    const float sx=std::sin(rotation.x),cx=std::cos(rotation.x),sy=std::sin(rotation.y),cy=std::cos(rotation.y),sz=std::sin(rotation.z),cz=std::cos(rotation.z);
    const math::Matrix4 x{{math::Vector4{1,0,0,0},{0,cx,sx,0},{0,-sx,cx,0},{0,0,0,1}}};
    const math::Matrix4 y{{math::Vector4{cy,0,-sy,0},{0,1,0,0},{sy,0,cy,0},{0,0,0,1}}};
    const math::Matrix4 z{{math::Vector4{cz,sz,0,0},{-sz,cz,0,0},{0,0,1,0},{0,0,0,1}}};
    // Wrapper update 1d77b0 applies Z, X, then Y to each matrix column.
    auto result=math::multiply(y,math::multiply(x,z));result.columns[3]=position;result.columns[3].w=1;return result;
}
math::Matrix4 TransformMotion::cylindrical_matrix()const{
    constexpr float radius=729.5f;const float angle=position.z/radius;
    TransformMotion x,y,z;x.rotation.x=-angle;y.rotation.y=rotation.y;z.rotation.z=rotation.z;
    auto world=math::multiply(x.matrix(),math::multiply(y.matrix(),z.matrix()));
    world.columns[3]={position.x,radius-(radius-position.y)*std::cos(angle),(radius-position.y)*std::sin(angle),1};
    return world;
}
}
