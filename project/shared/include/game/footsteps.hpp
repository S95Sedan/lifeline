#pragma once
#include "core/math.hpp"
#include <functional>
#include <optional>
#include <span>
namespace lifeline {
// Contact hysteresis and sequence selection recovered from 100738.
class Footsteps {
    bool left_=false,right_=false;
public:
    bool enabled=false;
    int surface=0;
    std::optional<unsigned> advance(std::span<const math::Matrix4> world_pose,float actor_y,
        unsigned animation,int scene,int world_surface,bool curved_world,const std::function<unsigned()>& random);
};
}
