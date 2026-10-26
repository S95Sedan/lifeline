#pragma once
#include "game/animation.hpp"
#include <array>
namespace lifeline {
// Native body/head/eye aiming layered onto an animated skeleton.
class Gaze {
    struct Joint {int mode=-1;math::Vector4 direction{},target{};bool initialized=false;};
    std::array<Joint,6> joints_;
    int body_mode_=-1;
    math::Vector4 body_target_{},body_direction_{};
    unsigned requested_modes_=0;
    math::Vector4 target_{0,0,1,1};
    bool relative_=false;
    float speed_=1;
public:
    void speed(float);
    float speed()const{return speed_;}
    void modes(int eyes,int head,int body=0);
    unsigned modes()const;
    int head_mode()const{return joints_[3].mode;}
    void target(math::Vector4,bool direction=false);
    void apply(const Skeleton&,std::span<math::Matrix4> world_pose);
};
}
