#pragma once
#include "script/runtime.hpp"
#include "rendering/model.hpp"
#include "game/camera_track.hpp"
#include "assets/resources.hpp"

namespace lifeline::script {
// Camera coordinates stay in authored space; reflection belongs to rendering.
class Camera {
    SceneCamera current_;
    math::Vector4 eye_{0,0,0,1},target_{0,0,0,1};
    math::Vector4 local_eye_{0,0,0,1},local_target_{0,0,0,1};
    unsigned mode_=6;
    bool smooth_=true,cut_=false;
    float rate_=.1f,limit_=1.f,focal_=554.f;
    Resources* resources_=nullptr;
    std::optional<CameraTrack> track_;
    float track_frame_=0,track_step_=1;
    bool automatic_up_=true;
    math::Vector4 up_{0,1,0,0};
    std::optional<math::Matrix4> track_origin_;
    std::function<math::Matrix4()> actor_transform_;
    std::function<std::optional<math::Matrix4>(unsigned,bool)> object_transform_;
    unsigned tracked_object_=0;
    bool shaking_=false;
    float shake_amplitude_=0;
    unsigned update_phase_=0;
    std::array<int,2> display_offset_{};
public:
    // kernel.cb 0x6338: scene teardown disables fog without changing its
    // color/range. The next room may explicitly enable it again.
    void end_scene(){current_.fog_enabled=false;}
    Camera();
    void resources(Resources& resources){resources_=&resources;}
    void actor_transform(std::function<math::Matrix4()> transform){actor_transform_=std::move(transform);}
    void object_transform(std::function<std::optional<math::Matrix4>(unsigned,bool)> transform){object_transform_=std::move(transform);}
    Bindings bindings();
    void advance(std::span<const math::Matrix4> actor_pose,std::optional<math::Matrix4> actor_world={});
    void initialize(const SceneCamera&);
    const SceneCamera& current()const{return current_;}
    unsigned mode()const{return mode_;}
    const std::array<int,2>& display_offset()const{return display_offset_;}
};
}
