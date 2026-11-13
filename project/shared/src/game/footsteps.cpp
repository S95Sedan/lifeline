#include "game/footsteps.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace lifeline {
std::optional<unsigned> Footsteps::advance(std::span<const math::Matrix4> pose,float actor_y,unsigned animation,
    int scene,int world_surface,bool curved_world,const std::function<unsigned()>& random){
    if(!enabled || pose.empty())return {};
    if(pose.size()<=8 || !std::isfinite(actor_y))throw std::runtime_error("Invalid actor footstep pose");
    const math::Vector4 offset{0,std::bit_cast<float>(0xbf73f7cfu),std::bit_cast<float>(0xbf19999au),1};
    auto left=math::transform(pose[4],offset),right=math::transform(pose[8],offset);
    if(curved_world){
        const float radius=std::bit_cast<float>(0x44366000u);
        for(auto* foot:{&left,&right})foot->y=radius-std::sqrt(foot->z*foot->z+(radius-foot->y)*(radius-foot->y));
    }
    if(!std::isfinite(left.y) || !std::isfinite(right.y))throw std::runtime_error("Non-finite foot contact");
    bool contact=false;
    const auto update=[&](bool& latched,float y){
        if(latched){if(y>actor_y+.2f)latched=false;}
        else if(y<=actor_y+.08f){latched=true;contact=true;}
    };
    update(left_,left.y);update(right_,right.y);
    if(!contact)return {};
    // The branch-likely at 10093c forces the fallback for scene >= 61.
    const int material=scene>=61 || world_surface==-1?surface:world_surface;
    if(material<0 || material>15 || !random)throw std::runtime_error("Invalid footstep surface/random service");
    return unsigned(material)*8+(animation==87 || animation==94?4:0)+(random()&3);
}
}
