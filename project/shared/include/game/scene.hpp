#pragma once
#include "game/animation.hpp"
#include "rendering/renderer.hpp"
#include "game/objects.hpp"

namespace lifeline {
// Geometry only: the script's native actor/camera services own all motion.
class ActorScene {
    StaticModel scene_,room_,actor_;
    Skeleton skeleton_;
    SkinnedGeometry skin_;
    std::vector<ShadowCaster> shadow_casters_;
    ShadowSettings shadow_settings_;
    unsigned shadow_size_=0;
    bool shadow_valid_=false;
public:
    explicit ActorScene(std::span<const unsigned char> actor);
    ActorScene(std::span<const unsigned char> room,std::span<const unsigned char> actor);
    AssetImage render(const SceneCamera&,std::span<const math::Matrix4>,bool visible,const script::Objects::Instance* room=nullptr,const script::Objects* objects=nullptr,const StaticModel* effects=nullptr,bool transparent=false,SceneRenderer* renderer=nullptr,bool high_resolution=false);
    void render_into(AssetImage&,const SceneCamera&,std::span<const math::Matrix4>,bool visible,const script::Objects::Instance* room=nullptr,const script::Objects* objects=nullptr,const StaticModel* effects=nullptr,bool transparent=false,SceneRenderer* renderer=nullptr,bool high_resolution=false);
};
}
