#pragma once
#include "game/collision_grid.hpp"
#include "script/runtime.hpp"
#include "assets/resources.hpp"
#include "game/animation.hpp"
#include "game/transform_motion.hpp"
#include "game/footsteps.hpp"
#include "game/gaze.hpp"
#include "game/displacement.hpp"
#include "game/routes.hpp"
#include "game/action_table.hpp"
#include <array>
#include <optional>

namespace lifeline::script {
class Sound;
// Named models owned by the original process's 32 object slots. These are
// native meshes and poses, not addresses in a console object heap.
class Objects {
public:
    struct ActorGeometry {SkinnedGeometry skin;StaticModel surface;};
    struct Node {
        int source=-1;
        int state=-1;
        bool visible=false;
        bool scene_lighting=false;
        int rotation_order=0;
        int fog_mode=1;
        float fog_near=0,fog_far=50;
        math::Vector4 fog_color{128,0,0,0};
        std::optional<std::array<math::Vector4,2>> culling_box;
        float brightness=1;
        math::Vector4 tint{1,1,1,1};
        math::Vector4 position{0,0,0,1},rotation{0,0,0,0},pivot{0,0,0,1};
        math::Vector4 basis_sign{1,1,1,0};
        math::Matrix4 local_matrix()const{
            // 2b0e08 selects one of the six Euler orders used by 1e17c0.
            constexpr unsigned orders[6][3]{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
            auto matrix=math::identity();
            if(rotation_order>=0&&rotation_order<6)for(unsigned axis:orders[rotation_order]){
                TransformMotion local;
                if(axis==0)local.rotation.x=rotation.x;else if(axis==1)local.rotation.y=rotation.y;else local.rotation.z=rotation.z;
                matrix=math::multiply(local.matrix(),matrix);
            }
            matrix.columns[3]=position;
            matrix.columns[0]=matrix.columns[0]*basis_sign.x;matrix.columns[1]=matrix.columns[1]*basis_sign.y;matrix.columns[2]=matrix.columns[2]*basis_sign.z;
            return matrix;
        }
        math::Matrix4 bind=math::identity();
        std::optional<math::Matrix4> attached_world;
    };
    struct Instance {
        struct GeometryNode {
            bool present=false;
            math::Matrix4 bind=math::identity();
            math::Vector4 pivot{0,0,0,1},tint{1,1,1,1};
            math::Vector4 center{0,0,0,1};
        };
        struct Attachment {int parent=-1;unsigned bone=0,first_channel=0;};
        struct Motion {
            // nullopt targets the whole object; an index targets a node.
            std::optional<unsigned> node;
            math::Vector4 start{0,0,0,0},target{0,0,0,0};
            math::Vector4 direction{0,0,0,0},rotation{0,0,0,0};
            unsigned elapsed=0,duration=0;
            bool moving=false;
            TransformMotion controller;
        };
        std::string name;
        StaticModel model;
        std::vector<GeometryNode> geometry_nodes;
        TransformMotion transform;
        math::Vector4 scale{1,1,1,0};
        std::optional<math::Matrix4> attached_world;
        std::optional<Skeleton> skeleton;
        std::optional<SkinnedGeometry> skin;
        std::vector<Node> nodes;
        std::vector<Motion> motion;
        std::optional<Attachment> attachment;
        bool visible=false;
        bool nodes_initialized=false;
        int shadow_policy=1;
        bool cylindrical=false;
        ModelRefraction refraction;
        unsigned material_variant=0;
        std::shared_ptr<const AnimationBank> animations,secondary_animations;
        std::shared_ptr<const SkeletalClip> playing_clip,prior_clip;
        std::optional<AnimationCursor> cursor,previous_cursor;
        std::vector<math::Matrix4> skeletal_pose;
        std::map<unsigned,math::Matrix4> pose_overrides;
        unsigned clip=0,previous_clip=0;
        float blend=1,blend_step=1;
        AnimationDisplacement displacement;
        bool animation_paused=false,displacement_suppressed=false,root_pose_disabled=false;
        struct PendingAnimation {unsigned id,mode;float speed,transition;std::optional<float> start;bool force;};
        bool hold_completed_animation=false;
        std::optional<PendingAnimation> pending_animation;
        void load_animations(AnimationBank);
        int select_animation(unsigned,unsigned,float,float,std::optional<float> start={},bool force=false);
        void advance_skeleton();
        Instance(const ResourceFile&,const std::string&);
        std::vector<unsigned char> visibility() const;
        unsigned render_node_count()const{return nodes_initialized?unsigned(nodes.size()):model.nodes;}
        ModelMesh transformed_mesh(const ModelMesh&) const;
        ModelMesh transformed_mesh(const ModelMesh&,unsigned destination) const;
        std::vector<ModelMesh> render_meshes() const;
        math::Matrix4 world_matrix(bool animated_root=false)const;
    };
private:
    Resources& resources_;
    void allocate_nodes(unsigned object,unsigned count);
    std::array<std::unique_ptr<Instance>,32> slots_;
    std::array<std::array<float,16>,32> object_values_{};
    struct MotionSound {
        struct Cue {unsigned animation,group,sequence;bool tracked;};
        std::vector<Cue> cues;
        unsigned channel=0,bank=0;
        bool tracked=false;
    };
    std::array<MotionSound,32> motion_sounds_;
    Sound* sound_=nullptr;
    Bindings motion_sound_bindings();
    Bindings object_motion_bindings();
    void clear_motion_sound(unsigned);
    void play_motion_sound(unsigned,unsigned);
    std::optional<Skeleton> actor_;
    std::optional<ActorGeometry> actor_geometry_;
    ModelRefraction actor_refraction_;
    int actor_shadow_policy_=1;
    math::Vector4 actor_material_bias_{}; // Additive RGB in original 0..128 modulation units.
    std::array<std::shared_ptr<const AnimationBank>,2> actor_banks_;
    TransformMotion actor_motion_;
    AnimationDisplacement actor_displacement_;
    FacialExpression actor_expression_;
    bool actor_expression_enabled_=false;
    Footsteps footsteps_;
    Gaze gaze_;
    std::optional<AnimationCursor> actor_cursor_,previous_cursor_;
    // Original controller +44 retains the selected clip across bank replacement.
    std::shared_ptr<const SkeletalClip> actor_playing_clip_,actor_prior_clip_;
    unsigned actor_clip_=0,previous_clip_=0,animation_wait_=0,gaze_wait_=0;
    float actor_blend_=1,actor_blend_step_=0;
    bool actor_visible_=true;
    bool actor_cylindrical_=false;
    bool actor_commands_enabled_=false;
    // Actor-service option written by 492/493; distinct from command enable.
    // No consumer has been identified in the recovered actor update path.
    bool actor_script_option_=false;
    bool automatic_commands_=false,advance_route_pending_=false,endpoint_events_=true;
    int command_input_=-1,previous_action_mode_=0,deferred_command_=-1,endpoint_event_base_=0;
    std::array<std::int32_t,4> actor_values_{};
    std::array<float,16> actor_data_{}; // Process +7fb0, actor instance +30.
    std::function<void(State*)> command_owner_;
    bool displacement_suppressed_=false,root_pose_disabled_=false;
    std::optional<math::Vector4> navigation_target_;
    math::Vector4 navigation_origin_{};
    std::optional<float> ground_height_;
    bool vertical_scene_handoff_=false;
    float navigation_turn_limit_=0;
    float navigation_heading_offset_=0;
    float navigation_radius_squared_=16.f;
    SceneLights lights_;
    Routes routes_;
    // 491 also selects the actor-area table. Retained pending recovery of its
    // movement consumer; the dynamic collision service consumes its own grid.
    CollisionGrid navigation_grid_;
    ActionTable actions_;
    int route_mode_=0;
    struct ExitApproach { unsigned point; };
    std::optional<ExitApproach> exit_approach_;
    std::optional<float> exit_approach_radius_;
    std::optional<int> lab_entry_route_;
    bool lab_entry_idle_=false;
    math::Vector4 route_target_{0,0,0,1};
    struct ObjectAnimation {
        struct Frame {math::Vector4 position,rotation;};
        int object=-1,mode=-1;
        unsigned frame_count=0;
        std::vector<unsigned> channels;
        std::vector<int> nodes;
        std::vector<Frame> frames;
        float position=0,speed=1;
    };
    std::vector<ObjectAnimation> object_animations_;
    bool cylinder_room_enabled_=false;
    FlagBank room_flags_;
    struct ActorAttachment {int object=-1,bone=-1,node=-1;math::Vector4 offset{0,0,0,1};};
    std::array<ActorAttachment,8> actor_attachments_;
    std::array<std::array<ActorAttachment,8>,32> object_attachments_;
    void apply_object_attachments();
    Bindings object_attachment_bindings();
    Bindings animation_bindings();
    Bindings model_animation_bindings();
    void advance_object_animations();
    std::int32_t actor_command_=-1;
    std::map<unsigned,math::Matrix4> actor_pose_overrides_;
    std::vector<math::Matrix4> current_pose_;
    std::function<math::Vector4()> camera_eye_;
    const SkeletalClip& clip(unsigned)const;
    int animate(unsigned,unsigned,float,float,std::optional<float> start={},bool force=false);
    static unsigned slot(float);
    Instance& get(float);
public:
    float effect_ground_height(math::Vector4 origin)const{
        if(actor_&&actor_cylindrical_){const math::Vector4 radial{0,origin.y-729.5f,origin.z,0};return 729.5f-std::sqrt(math::dot_xyz(radial,radial));}
        return origin.y;
    }
    math::Vector4 effect_ground(math::Vector4 origin)const{
        // 18e768 with height zero, including its cylindrical-space origin.
        if(actor_&&actor_cylindrical_)return math::Vector4{0,-729.5f,origin.z,1}+effect_gravity(origin)*729.5f;
        return {origin.x,0,origin.z,1};
    }
    math::Vector4 effect_gravity(math::Vector4 origin)const{
        // 18e630: the current actor's cylindrical flag selects radial gravity.
        return actor_&&actor_cylindrical_?math::normalize_xyz(math::Vector4{0,origin.y-729.5f,origin.z,0}):math::Vector4{0,-1,0,0};
    }
    int actor_shadow_policy()const{return actor_shadow_policy_;}
    const ActorGeometry* actor_geometry()const{return actor_geometry_?&*actor_geometry_:nullptr;}
    const ModelRefraction& actor_refraction()const{return actor_refraction_;}
    const math::Vector4& actor_material_bias()const{return actor_material_bias_;}
    const SceneLights& lights()const{return lights_;}
    const Skeleton* actor_skeleton()const{return actor_?&*actor_:nullptr;}
    explicit Objects(Resources& resources):resources_(resources){}
    void sound(Sound& sound){sound_=&sound;}
    Bindings bindings();
    void routes(Bytes data){routes_.load(std::move(data));}
    const std::array<std::int32_t,4>& scene_entry()const{return actor_values_;}
    void scene_entry(std::array<std::int32_t,4> value){actor_values_=value;}
    void lab_entry_route(int route){lab_entry_route_=route;}
    bool finish_lab_entry(){
        if(!lab_entry_route_)return false;
        routes_.select(*lab_entry_route_,0,0,false,false);
        routes_.advance(0);
        route_target_=routes_.target();
        const auto direction=routes_.travel_direction();
        restore_actor_location(route_target_,{0,std::atan2(direction.x,direction.z),0,0},true,92);
        advance_route_pending_=false;lab_entry_route_.reset();lab_entry_idle_=true;return true;
    }
    void testing_room_exit_command(int command){
        if(command<0)return;
        if(exit_approach_radius_){navigation_radius_squared_=*exit_approach_radius_;exit_approach_radius_.reset();}
        exit_approach_.reset();
        if(command==2044)exit_approach_=ExitApproach{0};
        if(command==2045)exit_approach_=ExitApproach{34};
    }
    bool exit_approach_arrived()const{
        if(!exit_approach_)return false;
        const auto delta=routes_.position(exit_approach_->point)+actor_motion_.position*(-1);
        return math::dot_xyz(delta,delta)<=16.f;
    }
    bool finish_exit_arrival(){
        // Clear Exit's doorway by three world units, retaining the installed
        // route target for subsequent commands instead of continuing through
        // the corridor's uninterruptible passage events.
        if(routes_.route()!=38||animation()!=94||route_mode_!=1)return false;
        const auto delta=actor_motion_.position+routes_.position(75)*(-1);
        if(math::dot_xyz(delta,delta)<9.f)return false;
        restore_actor_location(actor_motion_.position,actor_motion_.rotation,true,92);
        advance_route_pending_=false;
        return true;
    }
    bool orient_scene_entry(){
        if(routes_.route()!=actor_values_[0]||routes_.point()!=actor_values_[2])return false;
        const auto direction=routes_.travel_direction();
        actor_motion_.stop_turn(1);actor_motion_.rotation.y=std::atan2(direction.x,direction.z);return true;
    }
    // 1279dc..127a64: route/control state belongs to the scene script
    // context, which is recreated on entry, not the shared Rio model.
    void begin_route_context(){
        lab_entry_route_.reset();
        lab_entry_idle_=false;
        exit_approach_.reset();
        if(exit_approach_radius_){navigation_radius_squared_=*exit_approach_radius_;exit_approach_radius_.reset();}
        routes_=Routes{};route_target_={};route_mode_=previous_action_mode_=0;
        automatic_commands_=actor_commands_enabled_=advance_route_pending_=false;
        command_input_=deferred_command_=-1;
        navigation_target_.reset();navigation_origin_=actor_motion_.position;ground_height_=actor_motion_.position.y;vertical_scene_handoff_=false;
    }
    void navigation_grid(Bytes data){navigation_grid_.load(std::move(data));}
    void reset_navigation_grid(){navigation_grid_.reset();}
    TransformMotion& actor_motion(){return actor_motion_;}
    const TransformMotion& actor_motion()const{return actor_motion_;}
    void restore_actor_location(math::Vector4 position,math::Vector4 rotation,bool standing=false,unsigned idle_animation=0){
        actor_motion_.position=position;actor_motion_.rotation=rotation;
        actor_motion_.stop_translation();for(unsigned axis=0;axis<3;++axis)actor_motion_.stop_turn(axis);
        navigation_target_.reset();navigation_origin_=actor_motion_.position;ground_height_=position.y;vertical_scene_handoff_=false;
        // A zero blend step retains the previous pose forever. Save scene 100
        // leaves that previous pose seated, so select the standing idle with
        // a completed blend instead of merely changing the reported clip ID.
        if(standing){route_mode_=previous_action_mode_=0;deferred_command_=-1;animate(idle_animation,1,.5f,1.f,{},false);}
    }
    FacialExpression& actor_expression(){return actor_expression_;}
#ifdef LIFELINE_DEV_SCENE_PROBE
    float debug_actor_frame()const{return actor_cursor_?actor_cursor_->position():0;}
    std::array<float,12> debug_route_state()const{return {float(routes_.route()),float(routes_.point()),float(routes_.reverse()),float(route_mode_),float(advance_route_pending_),float(navigation_target_.has_value()),navigation_target_?navigation_target_->x:0,navigation_target_?navigation_target_->z:0,route_target_.x,route_target_.z,navigation_radius_squared_,float(displacement_suppressed_)};}
    std::array<int,6> debug_command_state()const{return {int(automatic_commands_),int(actor_commands_enabled_),int(endpoint_events_),endpoint_event_base_,previous_action_mode_,deferred_command_};}
#endif
    bool actor_expression_enabled()const{return actor_expression_enabled_;}
    Footsteps& footsteps(){return footsteps_;}
    Gaze& gaze(){return gaze_;}
    void advance();
    void update_room_visibility(math::Vector4 eye);
    void apply_actor_attachments(std::span<const math::Matrix4>);
    void advance_skeletal_attachments(std::span<const math::Matrix4>);
    void command_owner(std::function<void(State*)> callback){command_owner_=std::move(callback);}
    void command_input(int value){command_input_=value;}
    // Keeps a completed object's final pose visible while an authored close-up
    // finishes. A same-clip restart is applied when the owning view closes.
    void hold_completed_object_animation(unsigned object,bool hold);
    std::optional<unsigned> update_commands(bool command_running);
    void camera_eye(std::function<math::Vector4()> value){camera_eye_=std::move(value);}
    std::span<const math::Matrix4> current_pose()const{return current_pose_;}
    bool actor_visible()const{return actor_visible_;}
    bool actor_script_option()const{return actor_script_option_;}
    std::int32_t actor_command()const{return actor_command_;}
    unsigned animation()const{return actor_clip_;}
    bool actor_transitioning()const{return previous_cursor_.has_value();}
    void actor(const Skeleton& skeleton){actor_=skeleton;}
    math::Matrix4 actor_world()const{return actor_cylindrical_?actor_motion_.cylindrical_matrix():actor_motion_.matrix();}
    void actor_bank(unsigned index,AnimationBank bank){actor_banks_.at(index)=std::make_shared<AnimationBank>(std::move(bank));}
    const AnimationBank& actor_bank(unsigned index)const;
    std::vector<math::Matrix4> actor_pose(unsigned animation,float frame)const;
    std::vector<math::Matrix4> update_actor_pose(unsigned animation,float frame);
    void pose_attachment(unsigned id,const SkeletalClip&,float frame,std::span<const math::Matrix4> parent_pose);
    const Instance* instance(unsigned id)const{return slots_.at(id).get();}
#ifdef LIFELINE_DEV_SCENE_PROBE
    struct DebugObjectAnimation {unsigned slot;int mode;float frame;std::vector<int> nodes;};
    std::vector<DebugObjectAnimation> debug_object_animations()const{
        std::vector<DebugObjectAnimation> result;
        for(unsigned slot=0;slot<object_animations_.size();++slot){const auto& animation=object_animations_[slot];if(animation.object>=0)result.push_back({slot,animation.mode,animation.position,animation.nodes});}
        return result;
    }
#endif
    AssetImage render(unsigned id,const SceneCamera&,bool transparent=true)const;
};
}
