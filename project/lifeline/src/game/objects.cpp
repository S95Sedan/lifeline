#include "rendering/renderer.hpp"
#include "game/objects.hpp"
#include "rendering/lighting.hpp"
#include <cmath>
#include <bit>
#include <numbers>
#include <chrono>
#include <iostream>

namespace lifeline::script {
namespace {
math::Matrix4 inverse_affine(const math::Matrix4& matrix){
    const auto& c=matrix.columns;const auto bc=math::cross_xyz(c[1],c[2]);const float determinant=math::dot_xyz(c[0],bc);
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f)throw std::runtime_error("Singular object node bind transform");
    auto rows=math::Matrix4{{bc*(1/determinant),math::cross_xyz(c[2],c[0])*(1/determinant),math::cross_xyz(c[0],c[1])*(1/determinant),{0,0,0,1}}};
    auto inverse=math::transpose(rows);inverse.columns[3]=math::transform(inverse,{ -c[3].x,-c[3].y,-c[3].z,1});return inverse;
}
}
std::optional<unsigned> Objects::update_commands(bool command_running){
    if(!automatic_commands_)return {};
    // Doorway arrivals are idle, not a request to run the endpoint's next
    // automatic passage action. Release ownership on the next spoken command.
    if(lab_entry_idle_){if(command_input_==-1)return {};lab_entry_idle_=false;}
    if(previous_action_mode_==7 && route_mode_!=7 && deferred_command_!=-1){command_input_=deferred_command_;deferred_command_=-1;}
    previous_action_mode_=route_mode_;
    int event=-1;
    if(actor_commands_enabled_ && command_input_!=-1){
        if(route_mode_==7)deferred_command_=command_input_;
        else event=actions_.event(route_mode_,routes_.segment(),command_input_);
    }
    if(event!=-1){deferred_command_=-1;advance_route_pending_=false;return unsigned(event);}
    if(command_running)return {};
    if(advance_route_pending_){
        event=endpoint_events_?routes_.endpoint_event():-1;
        if(event!=-1)event+=endpoint_event_base_;
        else{
            const auto timer=std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
            routes_.advance(((timer*1103515245u+12345u)>>16)&1);route_target_=routes_.target();ground_height_=route_target_.y;
        }
    }
    advance_route_pending_=false;
    return unsigned(event==-1?0:event);
}
const SkeletalClip& Objects::clip(unsigned animation)const{
    const auto& bank=actor_bank(animation>=1000?1:0);const auto index=animation>=1000?animation-1000:animation;
    if(index>=bank.clips.size())throw std::runtime_error("Actor animation "+std::to_string(animation)+" is outside bank "+std::to_string(animation>=1000?1:0)+" ("+std::to_string(bank.clips.size())+" clips)");return bank.clips[index];
}
int Objects::animate(unsigned animation,unsigned loop,float speed,float blend,std::optional<float> start,bool force){
    if(!actor_)throw std::runtime_error("Actor skeleton is not available");
    // Reissuing a playing clip with the same speed changes only its loop mode.
    if(!force && actor_cursor_ && actor_cursor_->playing() && actor_clip_==animation && actor_cursor_->speed()==speed && (!start || std::trunc(actor_cursor_->position())==*start)){
        const bool changed=actor_cursor_->mode()!=loop;actor_cursor_->mode(loop);return changed?1:0;
    }
    const auto& selected=clip(animation);
    auto selected_clip=std::shared_ptr<const SkeletalClip>(actor_banks_[animation>=1000?1:0],&selected);
    auto cursor=AnimationCursor(selected,loop,speed,start.value_or(-1.f));
    if(!std::isfinite(blend))throw std::runtime_error("Invalid actor blend speed");
    actor_displacement_.select();
    previous_cursor_=actor_cursor_;previous_clip_=actor_clip_;actor_prior_clip_=actor_playing_clip_;
    actor_clip_=animation;actor_cursor_=cursor;actor_playing_clip_=std::move(selected_clip);
    actor_blend_=blend>=0 && blend<1 && previous_cursor_?0.f:1.f;actor_blend_step_=blend;
    if(actor_blend_==1){previous_cursor_.reset();actor_prior_clip_.reset();}return int(animation);
}
void Objects::advance(){
    for(auto& object:slots_)if(object)for(auto& motion:object->motion)if(motion.node){
        const bool moving=motion.controller.moving();
        const bool turning=motion.controller.turning(0)||motion.controller.turning(1)||motion.controller.turning(2);
        if(moving||turning){
            auto& node=object->nodes.at(*motion.node);motion.controller.advance();
            if(moving)node.position=motion.controller.position;
            if(turning){node.rotation=motion.controller.rotation;node.basis_sign={1,1,1,0};}
        }
    }
    for(auto& object:slots_)if(object)object->transform.advance();
    advance_object_animations();
    for(auto& object:slots_)if(object)object->advance_skeleton();
    actor_motion_.advance();if(!actor_cursor_){actor_pose_overrides_.clear();apply_actor_attachments(current_pose_);return;}
    if(actor_clip_>=1000){
        const auto& frames=actor_playing_clip_->frames;
        if(!frames.empty()&&std::abs(frames.back().root_motion.y-frames.front().root_motion.y)>.01f)vertical_scene_handoff_=true;
    }
    else if(vertical_scene_handoff_){
        // Scene clips own their authored vertical root while playing. On any
        // return to an ordinary actor clip, reconcile the wrapper with the
        // current route; this applies to stairs, ramps, drops and later rooms
        // without depending on a particular animation identifier.
        if(navigation_target_)actor_motion_.position.y=navigation_target_->y;
        else if(ground_height_)actor_motion_.position.y=*ground_height_;
        navigation_origin_=actor_motion_.position;
        vertical_scene_handoff_=false;
    }
    if(navigation_target_){
        const auto delta=*navigation_target_+actor_motion_.position*(-1);
        if(delta.x*delta.x+delta.z*delta.z>=navigation_radius_squared_){auto& yaw=actor_motion_.rotation.y;const auto error=TransformMotion::angle(std::atan2(delta.x,delta.z)+navigation_heading_offset_-yaw);yaw=TransformMotion::angle(yaw+std::clamp(error*actor_motion_.turn_gain(),-navigation_turn_limit_,navigation_turn_limit_));}
        else{actor_motion_.position.y=navigation_target_->y;navigation_target_.reset();}
    }
    const auto advance_root=[&](AnimationCursor& cursor,const SkeletalClip& animation){
        const auto sample=[&](float f){const int i=int(f);return animation.frame(i).root_motion*(1-(f-i))+animation.frame(i+1).root_motion*(f-i);};
        const auto before=cursor.position();const auto root=sample(before);cursor.advance();auto delta=sample(cursor.position())+root*(-1);
        if(cursor.mode()==1){
            if(cursor.speed()>0 && cursor.position()<before)delta=delta+animation.frames.back().root_motion+animation.frames.front().root_motion*(-1);
            if(cursor.speed()<0 && cursor.position()>before)delta=delta+animation.frames.front().root_motion+animation.frames.back().root_motion*(-1);
        }return delta;
    };
    const auto current=advance_root(*actor_cursor_,*actor_playing_clip_);
    std::optional<math::Vector4> prior;if(previous_cursor_)prior=advance_root(*previous_cursor_,*actor_prior_clip_);
    const auto delta=actor_displacement_.advance(current,prior,actor_blend_,displacement_suppressed_);
    if(previous_cursor_){actor_blend_=std::min(1.f,actor_blend_+actor_blend_step_);if(actor_blend_==1){previous_cursor_.reset();actor_prior_clip_.reset();}}
    if(!displacement_suppressed_){
        // 1c6580..1c65b0 transforms the local horizontal animation step by
        // the complete model basis. Pitch/roll matter in zero gravity: a
        // pitched swimming clip must descend through its hatch, not continue
        // horizontally into the wall. Keep this in actor coordinates before
        // the separate cylindrical-world conversion.
        const auto step=math::transform(actor_motion_.matrix(),{delta.x,0,delta.z,0});
        actor_motion_.position=actor_motion_.position+step;
    }
    if(navigation_target_&&actor_clip_<1000){
        // Route height is continuous state, just like route X/Z. Ordinary
        // navigation follows its authored slope while scene animations retain
        // full control of their own vertical root motion.
        const auto path=*navigation_target_+navigation_origin_*(-1.f);
        const auto travelled=actor_motion_.position+navigation_origin_*(-1.f);
        const float planar=path.x*path.x+path.z*path.z;
        if(planar>1e-6f){const float progress=std::clamp((travelled.x*path.x+travelled.z*path.z)/planar,0.f,1.f);actor_motion_.position.y=navigation_origin_.y+(navigation_target_->y-navigation_origin_.y)*progress;}
        else actor_motion_.position.y=navigation_target_->y;
    }
    const auto root_mode=root_pose_disabled_?RootMotion::disabled:RootMotion::height_only;
    auto pose=previous_cursor_?blend_skeleton(*actor_,*actor_prior_clip_,previous_cursor_->position(),*actor_playing_clip_,actor_cursor_->position(),actor_blend_,root_mode):sample_skeleton(*actor_,*actor_playing_clip_,actor_cursor_->position(),root_mode);
    if(actor_expression_enabled_)apply_facial_expression(*actor_,pose,actor_expression_);
    for(const auto& [bone,matrix]:actor_pose_overrides_)pose.at(bone)=matrix;
    actor_pose_overrides_.clear();
    const auto world=actor_world();for(auto& bone:pose)bone=math::multiply(world,bone);
    gaze_.apply(*actor_,pose);current_pose_=std::move(pose);
    apply_actor_attachments(current_pose_);
}
void Objects::apply_actor_attachments(std::span<const math::Matrix4> pose){
    for(auto& object:slots_)if(object){object->attached_world.reset();for(auto& node:object->nodes)node.attached_world.reset();}
    apply_object_attachments();
    for(const auto& attachment:actor_attachments_){
        if(attachment.object<0 || !slots_.at(unsigned(attachment.object)))continue;
        if(attachment.bone<0 || unsigned(attachment.bone)>=pose.size())throw std::runtime_error("Actor attachment bone outside current pose");
        auto& object=*slots_[unsigned(attachment.object)];
        auto* node=attachment.node>=0?&object.nodes.at(unsigned(attachment.node)):nullptr;
        const auto angles=node?node->rotation:object.transform.rotation;
        // 12c6bc builds X, then Y, then Z (ordinary objects use Z,X,Y).
        TransformMotion x,y,z;x.rotation.x=angles.x;y.rotation.y=angles.y;z.rotation.z=angles.z;
        auto local=math::multiply(z.matrix(),math::multiply(y.matrix(),x.matrix()));
        if(!node){local.columns[0]=local.columns[0]*object.scale.x;local.columns[1]=local.columns[1]*object.scale.y;local.columns[2]=local.columns[2]*object.scale.z;}
        local.columns[3]=node?attachment.offset:object.transform.position;local.columns[3].w=1;
        const auto world=math::multiply(pose[unsigned(attachment.bone)],local);
        if(node)node->attached_world=world;else object.attached_world=world;
    }
    advance_skeletal_attachments(pose);
}
const AnimationBank& Objects::actor_bank(unsigned index)const{
    const auto& bank=actor_banks_.at(index);if(!bank)throw std::runtime_error("Actor animation bank is not loaded");return *bank;
}
std::vector<math::Matrix4> Objects::actor_pose(unsigned animation,float frame)const{
    if(!actor_)throw std::runtime_error("Actor skeleton is not available");
    const auto& bank=actor_bank(animation>=1000?1:0);const auto index=animation>=1000?animation-1000:animation;
    if(index>=bank.clips.size())throw std::runtime_error("Actor animation "+std::to_string(animation)+" is outside bank "+std::to_string(animation>=1000?1:0)+" ("+std::to_string(bank.clips.size())+" clips)");
    auto pose=sample_skeleton(*actor_,bank.clips[index],frame);
    if(actor_expression_enabled_)apply_facial_expression(*actor_,pose,actor_expression_);
    const auto world=actor_world();for(auto& bone:pose)bone=math::multiply(world,bone);
    return pose;
}
std::vector<math::Matrix4> Objects::update_actor_pose(unsigned animation,float frame){
    auto pose=actor_pose(animation,frame);gaze_.apply(*actor_,pose);return pose;
}
Objects::Instance::Instance(const ResourceFile& file,const std::string& member):name(member){
    if(!file.bytes || file.bytes->size()<10)throw std::runtime_error("Missing model resource: "+name);
    const auto& bytes=*file.bytes;
    const std::string version(bytes.begin(),bytes.begin()+10);
    if(version=="SOLCOM0418" || version=="SOLCOM0419"){
        model=decode_static_model(bytes);geometry_nodes.resize(model.nodes);
        const auto read=[&](unsigned offset){return std::bit_cast<float>(le32(bytes.data()+offset));};
        for(unsigned i=0;i<model.nodes;++i){
            auto& node=geometry_nodes[i];const unsigned record=64+i*256;
            node.present=le16(bytes.data()+record+12)!=0;
            node.pivot={read(record+112),read(record+116),read(record+120),1};
            node.center={read(record+128),read(record+132),read(record+136),1};
            for(unsigned column=0;column<4;++column){const auto p=record+64+column*16;node.bind.columns[column]={read(p),read(p+4),read(p+8),column==3?1.f:0.f};}
            node.tint={read(record+144),read(record+148),read(record+152),read(record+156)};
        }
    }
    else if(version=="SKICOM0830" || version=="SKICOM0828" || version=="SKICOM0829"){
        skeleton=decode_skeleton(bytes);skin=decode_skinned_geometry(bytes,*skeleton);
        model=decode_skin_surface(bytes,*skin);
        // 1dbffc constructs the runtime bone matrices even when the object is
        // loaded without an animation bank. Effects and cameras can query that
        // bind pose immediately (the Molotov throw does exactly this).
        skeletal_pose.reserve(skeleton->bones.size());
        for(const auto& bone:skeleton->bones){
            if(bone.parent==-2){skeletal_pose.push_back(math::identity());continue;}
            auto local=math::identity();local.columns[3]=bone.translation;local.columns[3].w=1;
            skeletal_pose.push_back(bone.parent<0?local:math::multiply(skeletal_pose.at(unsigned(bone.parent)),local));
        }
        // Keep source skin geometry until an animation bank or attachment
        // supplies a sampled pose. Loading a model still starts no animation.
    }else throw std::runtime_error("Unsupported object model: "+version);
}
unsigned Objects::slot(float value){
    if(!std::isfinite(value) || value<0 || value>=32 || value!=std::floor(value))throw std::runtime_error("Invalid script object slot");
    return unsigned(value);
}
Objects::Instance& Objects::get(float value){
    const auto index=slot(value);auto& object=slots_[index];if(!object)throw std::runtime_error("Script object slot is empty: "+std::to_string(index));return *object;
}
std::vector<unsigned char> Objects::Instance::visibility()const{
    std::vector<unsigned char> result(render_node_count(),0);
    if(!visible)return result;
    if(!nodes_initialized){std::fill(result.begin(),result.end(),1);return result;}
    for(unsigned i=0;i<nodes.size();++i)if(nodes[i].visible && nodes[i].source>=0)result.at(i)=1;
    return result;
}
AssetImage Objects::render(unsigned id,const SceneCamera& camera,bool transparent)const{
    const auto* object=instance(id);if(!object)throw std::runtime_error("Cannot render empty script object slot");
    auto model=object->model;model.nodes=object->render_node_count();model.meshes=object->render_meshes();
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));
    if(object->skeleton)light_character(model,lights_.records(),lights_.groups(),object->world_matrix().columns[3],forward,camera.fill_light);
    else for(auto& mesh:model.meshes)if(mesh.scene_lighting)light_node(mesh,lights_.records(),lights_.groups(),forward,camera.fill_light);
    return render_scene(model,camera,object->visibility(),transparent);
}
math::Matrix4 Objects::Instance::world_matrix(bool animated_root)const{
    auto world=cylindrical?transform.cylindrical_matrix():transform.matrix();
    world.columns[0]=world.columns[0]*scale.x;world.columns[1]=world.columns[1]*scale.y;world.columns[2]=world.columns[2]*scale.z;
    if(attached_world)world=*attached_world;
    if(animated_root && skeleton){
        if(skeletal_pose.empty())throw std::runtime_error("Animated object camera requires a sampled root pose");
        world=math::multiply(world,skeletal_pose.front());
    }
    return world;
}
ModelMesh Objects::Instance::transformed_mesh(const ModelMesh& source)const{
    return transformed_mesh(source,source.node);
}
std::vector<ModelMesh> Objects::Instance::render_meshes()const{
    std::vector<ModelMesh> result;
    if(!nodes_initialized){for(const auto& mesh:model.meshes)result.push_back(transformed_mesh(mesh));return result;}
    for(unsigned destination=0;destination<nodes.size();++destination){
        const auto& node=nodes[destination];if(!node.visible || node.source<0)continue;
        for(const auto& mesh:model.meshes)if(mesh.node==unsigned(node.source))result.push_back(transformed_mesh(mesh,destination));
    }
    return result;
}
ModelMesh Objects::Instance::transformed_mesh(const ModelMesh& source,unsigned destination)const{
    auto mesh=source;const auto world=world_matrix();
    mesh.refraction=refraction;
    mesh.node=destination;
    const Node* node=nodes_initialized?&nodes.at(destination):nullptr;
    if(node){mesh.fog_mode=node->fog_mode;mesh.fog_near=node->fog_near;mesh.fog_far=node->fog_far;mesh.fog_color=node->fog_color;}
    // Empty bookkeeping nodes may have singular binds. Only geometry driven
    // by an attachment needs to undo its authored transform.
    std::optional<math::Matrix4> driven;
    if(node && node->attached_world && !mesh.vertices.empty())driven=math::multiply(*node->attached_world,inverse_affine(node->bind));
    std::optional<math::Matrix4> node_transform;
    if(node && !node->attached_world && !mesh.vertices.empty()){
        const int mode=node->state&7;
        if(mode>2){mesh.indices.clear();return mesh;}
        // SOLCOM vertices have their authored matrix baked by the decoder.
        // State 0 keeps it, 1 replaces it, and 2 composes bind * local.
        if(mode){
            const auto selected=mode==1?node->local_matrix():math::multiply(node->bind,node->local_matrix());
            node_transform=math::multiply(selected,inverse_affine(node->bind));
        }
    }
    if(node && node->scene_lighting && node->source>=0 && unsigned(node->source)<geometry_nodes.size()){
        mesh.scene_lighting=true;
        auto center=math::transform(node->bind,geometry_nodes[node->source].center);
        if(driven)center=math::transform(*driven,center);
        else {if(node_transform)center=math::transform(*node_transform,center);center=math::transform(world,center);}
        mesh.lighting_position=center;
    }
    if(node&&node->culling_box){
        const auto& box=*node->culling_box;std::array<math::Vector4,8> corners;
        for(unsigned i=0;i<8;++i){auto point=box[0]+math::Vector4{(i&1)?box[1].x:0,(i&2)?box[1].y:0,(i&4)?box[1].z:0,0};
            point=math::transform(node->bind,point);
            if(driven)point=math::transform(*driven,point);
            else {if(node_transform)point=math::transform(*node_transform,point);point=math::transform(world,point);}
            corners[i]=point;
        }
        mesh.culling_corners=corners;
    }
    for(auto& vertex:mesh.vertices){
        // Skin surfaces are stored in the preview renderer's Y-down convention.
        // Convert back before composing script/world transforms.
        if(skeleton){vertex.position.y=-vertex.position.y;vertex.normal.y=-vertex.normal.y;}
        if(node && node->attached_world){
            // Installed static vertices already include the authored bind.
            // A driven node replaces that matrix with the actor-bone matrix.
            vertex.position.w=1;vertex.position=math::transform(*driven,vertex.position);
            vertex.normal=math::transform(*driven,{vertex.normal.x,vertex.normal.y,vertex.normal.z,0});
            vertex.color.x*=node->brightness*node->tint.x;vertex.color.y*=node->brightness*node->tint.y;vertex.color.z*=node->brightness*node->tint.z;vertex.color.w*=node->tint.w;
            continue;
        }
        if(node){
            if(node_transform){
                vertex.position.w=1;vertex.position=math::transform(*node_transform,vertex.position);
                vertex.normal=math::transform(*node_transform,{vertex.normal.x,vertex.normal.y,vertex.normal.z,0});
            }
            vertex.color.x*=node->brightness*node->tint.x;vertex.color.y*=node->brightness*node->tint.y;vertex.color.z*=node->brightness*node->tint.z;vertex.color.w*=node->tint.w;
        }
        vertex.position.w=1;vertex.position=math::transform(world,vertex.position);
        vertex.normal=math::transform(world,{vertex.normal.x,vertex.normal.y,vertex.normal.z,0});
    }
    return mesh;
}
void Objects::pose_attachment(unsigned id,const SkeletalClip& clip,float frame,std::span<const math::Matrix4> parent_pose){
    auto& object=get(float(id));
    if(!object.attachment || !object.skeleton || !object.skin)throw std::runtime_error("Object has no skeletal attachment");
    const auto& attachment=*object.attachment;
    if(attachment.bone>=parent_pose.size())throw std::runtime_error("Attachment parent pose lacks target bone");
    auto pose=sample_attachment_skeleton(*object.skeleton,clip,frame,attachment.first_channel);
    for(auto& matrix:pose)matrix=math::multiply(parent_pose[attachment.bone],matrix);
    object.skeletal_pose=pose;
    pose_skin_surface(object.model,*object.skin,*object.skeleton,pose);
}
void Objects::allocate_nodes(unsigned id,unsigned count){
    auto& object=get(float(id));
    if(object.skeleton)throw std::runtime_error("Node setup requires a static object");
    std::vector<Node> replacement(count);
    // These bindings target the old allocation, not nodes in a replacement
    // pool that happen to have the same indices. Keep whole-object motion.
    std::erase_if(object.motion,[](const auto& motion){return motion.node.has_value();});
    for(auto& animation:object_animations_)if(animation.object==int(id))animation.nodes.clear();
    for(auto& attachment:actor_attachments_)if(attachment.object==int(id) && attachment.node>=0)attachment={};
    object.nodes=std::move(replacement);object.nodes_initialized=true;
}
Bindings Objects::bindings(){
    Bindings result;
    result.emplace(354,Binding{2,0,[this](const Invocation& c){
        for(float v:c.arguments)if(!std::isfinite(v)||double(std::trunc(v))<INT32_MIN||double(std::trunc(v))>INT32_MAX)throw std::runtime_error("Invalid model shadow policy");
        const int selector=int(c.arguments[0]),policy=std::max(0,int(c.arguments[1]));
        if(selector==-1){if(!actor_)throw std::runtime_error("Actor shadow policy requires a loaded actor");actor_shadow_policy_=policy;}
        else get(float(selector)).shadow_policy=policy;
        return Reply{false,0.f};
    }});
    for(unsigned opcode:{492u,493u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){
        actor_script_option_=opcode==492;return Reply{};
    }});
    result.emplace(322,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Room visibility requires process flags");
        cylinder_room_enabled_=c.operands[0]!=0;room_flags_=c.state->flags;return Reply{};
    }});
    const auto refraction=[this](float selector)->ModelRefraction&{
        if(!std::isfinite(selector))throw std::runtime_error("Invalid refraction model selector");
        selector=std::trunc(selector);
        if(selector==-1){if(!actor_)throw std::runtime_error("Actor skeleton is not available");return actor_refraction_;}
        auto& object=get(selector);
        if(!object.skeleton)throw std::runtime_error("Refraction requires a skeletal model");
        return object.refraction;
    };
    result.emplace(337,Binding{2,0,[refraction](const Invocation& c){
        const float value=c.arguments[1];if(!std::isfinite(value))throw std::runtime_error("Invalid refraction strength");
        refraction(c.arguments[0]).strength=unsigned(std::clamp(value,0.f,1.f)*128.f);
        return Reply{false,0.f};
    }});
    result.emplace(338,Binding{2,0,[refraction](const Invocation& c){
        const float value=c.arguments[1];if(!std::isfinite(value))throw std::runtime_error("Invalid refraction displacement");
        refraction(c.arguments[0]).normal_offset=value;return Reply{false,0.f};
    }});
    result.emplace(339,Binding{4,0,[refraction](const Invocation& c){
        const auto byte=[](float value){
            if(!std::isfinite(value) || double(value)<-2147483648. || double(value)>=2147483648.)throw std::runtime_error("Invalid refraction color");
            return float(std::uint32_t(std::int32_t(value))&255)/128.f;
        };
        const math::Vector4 tint{byte(c.arguments[1]),byte(c.arguments[2]),byte(c.arguments[3]),1};
        refraction(c.arguments[0]).tint=tint;return Reply{false,0.f};
    }});
    result.emplace(361,Binding{0,2,[this](const Invocation& c){
        // Decode before replacing the live actor: all resulting geometry and
        // animation storage is owned independently of the resource group.
        const auto& model=*resources_.find_raw(c.program.string(c.operands[0])).bytes;
        auto skeleton=decode_skeleton(model);ActorGeometry geometry;
        geometry.skin=decode_skinned_geometry(model,skeleton);geometry.surface=decode_skin_surface(model,geometry.skin);
        auto bank=std::make_shared<AnimationBank>(decode_animation_bank(*resources_.find_raw(c.program.string(c.operands[1])).bytes));
        if(bank->clips.empty())throw std::runtime_error("Replacement actor has no animation clips");
        auto pose=sample_skeleton(skeleton,bank->clips.front(),0,RootMotion::height_only);
        actor_=std::move(skeleton);actor_geometry_=std::move(geometry);actor_banks_.fill(bank);
        actor_cursor_.reset();previous_cursor_.reset();actor_playing_clip_.reset();actor_prior_clip_.reset();actor_clip_=previous_clip_=animation_wait_=gaze_wait_=0;
        actor_blend_=1;actor_blend_step_=0;actor_motion_={};actor_displacement_={};
        actor_expression_={};actor_expression_enabled_=false;gaze_={};
        actor_refraction_={};actor_material_bias_={};actor_shadow_policy_=1;
        actor_visible_=true;actor_cylindrical_=false;displacement_suppressed_=root_pose_disabled_=false;
        navigation_target_.reset();navigation_origin_=actor_motion_.position;ground_height_.reset();navigation_turn_limit_=navigation_heading_offset_=0;navigation_radius_squared_=16;vertical_scene_handoff_=false;
        current_pose_=std::move(pose);actor_pose_overrides_.clear();return Reply{};
    }});
    result.emplace(510,Binding{0,0,[this](const Invocation&){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        return Reply{false,actor_displacement_.horizontal(actor_blend_,displacement_suppressed_)};
    }});
    result.emplace(444,Binding{0,7,[this](const Invocation& c){
        const unsigned index=c.operands[0];if(index>=actor_attachments_.size())throw std::runtime_error("Invalid actor attachment entry");
        const unsigned object=slot(float(c.operands[1])),node_index=c.operands[2],bone=c.operands[3];
        auto& node=get(float(object)).nodes.at(node_index);math::Vector4 offset{std::bit_cast<float>(c.operands[4]),std::bit_cast<float>(c.operands[5]),std::bit_cast<float>(c.operands[6]),1};
        if(bone>32767 || !std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z))throw std::runtime_error("Invalid node attachment parameters");
        actor_attachments_[index]={int(object),int(bone),int(node_index),offset};
        node.rotation={0,0,0,0};node.basis_sign={1,1,1,0};node.state=node.source>=0?1:-1;node.visible=node.state>=0;return Reply{};
    }});
    result.emplace(446,Binding{3,0,[this](const Invocation& c){
        const auto index=c.arguments[0],object=c.arguments[1],bone=c.arguments[2];
        if(!std::isfinite(index)||index<0||index>=8||index!=std::trunc(index) || !std::isfinite(bone)||bone<0||bone>32767||bone!=std::trunc(bone))throw std::runtime_error("Invalid actor attachment entry/bone");
        if(object!=-1.f)slot(object);
        actor_attachments_[unsigned(index)]={int(object),int(bone)};return Reply{false,0.f};
    }});
    result.emplace(448,Binding{1,0,[this](const Invocation& c){
        const auto index=c.arguments[0];if(!std::isfinite(index)||index<0||index>=8||index!=std::trunc(index))throw std::runtime_error("Invalid actor attachment entry");
        actor_attachments_[unsigned(index)]={};return Reply{false,0.f};
    }});
    result.emplace(447,Binding{0,0,[this](const Invocation&){actor_attachments_.fill({});return Reply{};}});
    result.emplace(500,Binding{0,4,[this](const Invocation& c){for(unsigned i=0;i<4;++i)actor_values_[i]=std::bit_cast<std::int32_t>(c.operands[i]);return Reply{};}});
    for(unsigned opcode:{399u,400u,401u})result.emplace(opcode,Binding{0,2,[this,opcode](const Invocation& c){
        // 127d60/127da0/127de8: the actor's float data bank is separate
        // from the four integer values transferred by 500/501.
        const auto index=c.operands[opcode==401?1:0];
        if(index>=actor_data_.size())throw std::runtime_error("Invalid actor data cell");
        auto& value=actor_data_[index];
        if(opcode==401){if(!c.state)throw std::runtime_error("Actor data query requires script locals");c.state->floats[c.operands[0]]=value;}
        else if(opcode==399)value=std::bit_cast<float>(c.operands[1]);
        else value+=std::bit_cast<float>(c.operands[1]);
        return Reply{};
    }});
    result.emplace(501,Binding{0,4,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Actor data query requires script locals");
        for(unsigned i=0;i<4;++i)c.state->integers[c.operands[i]]=actor_values_[i];return Reply{};
    }});
    result.emplace(213,Binding{2,0,[this](const Invocation& c){
        const auto id=std::nearbyint(c.arguments[0]),enabled=std::nearbyint(c.arguments[1]);
        if(!std::isfinite(enabled))throw std::runtime_error("Invalid object cylindrical flag");
        get(id).cylindrical=enabled!=0;return Reply{false,id};
    }});
    result.emplace(214,Binding{0,5,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Object distance query requires script locals");
        const auto& position=get(float(c.operands[0])).transform.position;
        const float x=std::bit_cast<float>(c.operands[2])-position.x,y=std::bit_cast<float>(c.operands[3])-position.y,z=std::bit_cast<float>(c.operands[4])-position.z;
        c.state->floats[c.operands[1]]=std::sqrt(x*x+y*y+z*z);return Reply{};
    }});
    result.emplace(215,Binding{3,0,[this](const Invocation& c){
        const auto& position=get(float(std::nearbyint(c.arguments[0]))).transform.position;
        const float x=c.arguments[1]-position.x,z=c.arguments[2]-position.z;
        return Reply{false,std::sqrt(x*x+z*z)};
    }});
    for(unsigned opcode:{216u,217u,218u})result.emplace(opcode,Binding{0,3,[this,opcode](const Invocation& c){
        const unsigned object=c.operands[opcode==218?1:0],index=c.operands[opcode==218?2:1];
        if(object>=object_values_.size() || index>=16)throw std::runtime_error("Invalid object data cell");
        auto& value=object_values_[object][index];
        if(opcode==218){if(!c.state)throw std::runtime_error("Object data query requires script locals");c.state->floats[c.operands[0]]=value;}
        else if(opcode==216)value=std::bit_cast<float>(c.operands[2]);
        else value+=std::bit_cast<float>(c.operands[2]);return Reply{};
    }});
    for(unsigned axis=0;axis<3;++axis){
        result.emplace(207u+axis,Binding{2,0,[this,axis](const Invocation& c){
            const float value=c.arguments[1];if(!std::isfinite(value))throw std::runtime_error("Invalid object scale");
            auto& scale=get(c.arguments[0]).scale;(axis==0?scale.x:axis==1?scale.y:scale.z)=value;return Reply{false,value};
        }});
        result.emplace(210u+axis,Binding{0,2,[this,axis](const Invocation& c){
            if(!c.state)throw std::runtime_error("Object scale query requires script locals");
            const auto& scale=get(float(c.operands[0])).scale;c.state->floats[c.operands[1]]=axis==0?scale.x:axis==1?scale.y:scale.z;return Reply{};
        }});
        for(const bool rotation:{false,true}){
            result.emplace((rotation?195u:178u)+axis,Binding{2,0,[this,axis,rotation](const Invocation& c){
                const float value=c.arguments[1];if(!std::isfinite(value))throw std::runtime_error("Invalid object transform");
                auto& transform=get(c.arguments[0]).transform;auto& vector=rotation?transform.rotation:transform.position;
                (axis==0?vector.x:axis==1?vector.y:vector.z)=rotation?TransformMotion::angle(value):value;
                return Reply{false,value};
            }});
            result.emplace((rotation?198u:182u)+axis,Binding{0,2,[this,axis,rotation](const Invocation& c){
                if(!c.state)throw std::runtime_error("Object transform query requires script locals");
                const auto& transform=get(float(c.operands[0])).transform;const auto& vector=rotation?transform.rotation:transform.position;
                c.state->floats[c.operands[1]]=axis==0?vector.x:axis==1?vector.y:vector.z;return Reply{};
            }});
        }
    }
    result.emplace(345,Binding{0,0,[this](const Invocation&){
        for(unsigned i=0;i<slots_.size();++i)if(!slots_[i])return Reply{false,float(i)};
        return Reply{false,-1.f};
    }});
    result.emplace(350,Binding{1,0,[this](const Invocation& c){return Reply{false,slots_[slot(c.arguments[0])]?1.f:0.f};}});
    result.emplace(244,Binding{1,0,[this](const Invocation& c){
        // A skeletal child's sampled pose already includes the parent's world
        // transform. Detach before releasing that parent; the child remains at
        // its last pose until its own script removes or reattaches it.
        for(auto& child:slots_)if(child&&child->attachment&&child->attachment->parent==int(slot(c.arguments[0]))){
            child->attachment.reset();child->transform={};child->scale={1,1,1,1};child->cylindrical=false;
        }
        const auto id=slot(c.arguments[0]);clear_motion_sound(id);slots_[id].reset();object_values_[id].fill(0);return Reply{false,float(id)};
    }});
    result.emplace(245,Binding{0,0,[this](const Invocation&){
        for(unsigned i=0;i<slots_.size();++i){clear_motion_sound(i);slots_[i].reset();}object_values_={};object_animations_.clear();cylinder_room_enabled_=false;return Reply{};
    }});
    result.emplace(487,Binding{0,1,[this](const Invocation& c){
        const auto name=c.program.string(c.operands[0]);
        // 1acef0 asks the resource service for the ME member and returns zero
        // without replacing the active action table when lookup fails. Scene
        // 14 deliberately requests event014.me although that member is absent
        // from the retail disc archive.
        if(resources_.first_loaded_files(name))actions_.load(*resources_.find_raw(name).bytes);
        return Reply{};
    }});
    result.emplace(486,Binding{0,1,[this](const Invocation& c){
        const auto name=c.program.string(c.operands[0]);
        // 10f338 retains the existing route decoder when lookup fails.
        if(resources_.first_loaded_files(name))routes(*resources_.find_raw(name).bytes);
        return Reply{};
    }});
    result.emplace(479,Binding{0,1,[this](const Invocation& c){actions_.shared_base(std::bit_cast<int>(c.operands[0]));return Reply{};}});
    result.emplace(480,Binding{0,1,[this](const Invocation& c){actions_.scene_base(std::bit_cast<int>(c.operands[0]));return Reply{};}});
    result.emplace(366,Binding{0,0,[this](const Invocation&){
        actor_.reset();actor_geometry_.reset();actor_banks_.fill({});actor_cursor_.reset();previous_cursor_.reset();actor_playing_clip_.reset();actor_prior_clip_.reset();current_pose_.clear();actor_pose_overrides_.clear();
        actor_motion_={};actor_displacement_={};actor_expression_={};gaze_={};navigation_target_.reset();navigation_origin_={};ground_height_.reset();vertical_scene_handoff_=false;
        actor_visible_=actor_commands_enabled_=automatic_commands_=advance_route_pending_=false;
        command_input_=deferred_command_=-1;actor_attachments_.fill({});return Reply{};
    }});
    result.emplace(367,Binding{0,0,[this](const Invocation& c){
        if(!command_owner_ || !c.state)throw std::runtime_error("Automatic actor commands require a process owner");
        // The Lab's ordinary revisit worker hardcodes one placement for both
        // doors. Consume the corridor handoff at the end of that setup, before
        // its first visible frame. First-visit cinematics and save loads do not
        // use this override.
        if(lab_entry_route_&&c.program.name()=="entry_037.cb"&&c.address==0x9fd40/4){
            finish_lab_entry();
        }
        command_owner_(c.state);automatic_commands_=actor_commands_enabled_=true;return Reply{};
    }});
    result.emplace(470,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Action query requires script locals");
        c.state->integers[c.operands[0]]=command_input_==-1?-1:actions_.event(route_mode_,routes_.segment(),command_input_);return Reply{};
    }});
    result.emplace(478,Binding{0,1,[this](const Invocation& c){endpoint_event_base_=std::bit_cast<int>(c.operands[0]);return Reply{};}});
    for(unsigned opcode:{494u,495u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){endpoint_events_=opcode==494;return Reply{};}});
    result.emplace(471,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Route endpoint event requires script locals");
        const int event=endpoint_events_?routes_.endpoint_event():-1;c.state->integers[c.operands[0]]=event==-1?-1:event+endpoint_event_base_;return Reply{};
    }});
    for(unsigned opcode:{468u,469u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){advance_route_pending_=opcode==468;return Reply{};}});
    result.emplace(467,Binding{0,1,[this](const Invocation& c){if(!c.state)throw std::runtime_error("Route advance query requires script locals");c.state->integers[c.operands[0]]=advance_route_pending_;return Reply{};}});
    for(unsigned opcode:{481u,482u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){actor_commands_enabled_=opcode==481;return Reply{};}});
    result.emplace(450,Binding{1,0,[this](const Invocation& c){
        route_mode_=int(c.arguments[0]);
        if(c.program.name()=="entry_037.cb"&&c.address==0x98778/4)finish_lab_entry();
        return Reply{false,float(route_mode_)};
    }});
    result.emplace(451,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Route mode query requires script state");
        c.state->integers[c.operands[0]]=route_mode_;return Reply{};
    }});
    for(unsigned op=378;op<=396;++op){
        if(op==387)continue; // Aggregate vector returns have a different ABI.
        const bool position=op<388,store=op<=380 || (op>=388 && op<=390);
        const unsigned axis=position?(op-378)%3:(op-388)%3;
        result.emplace(op,Binding{0,store?1u:0u,[this,position,store,axis](const Invocation& c){
            const auto& vector=position?actor_motion_.position:actor_motion_.rotation;
            const float value=axis==0?vector.x:axis==1?vector.y:vector.z;
            if(!store)return Reply{false,value};
            if(!c.state)throw std::runtime_error("Actor transform query requires script state");
            c.state->floats[c.operands[0]]=value;return Reply{};
        }});
    }
    for(unsigned opcode:{452u,453u,454u,455u})result.emplace(opcode,Binding{0,opcode==454?2u:3u,[this,opcode](const Invocation& c){
        if(opcode==453 && !c.state)throw std::runtime_error("Route selection requires integer locals");
        const auto value=[&](unsigned i){return opcode==453?c.state->integers[c.operands[i]]:std::bit_cast<int>(c.operands[i]);};
        routes_.select(value(0),value(1),opcode==454?-1:value(2),opcode==454,opcode>=454);
        if(opcode<=453){actor_motion_.position=routes_.target();ground_height_=actor_motion_.position.y;}
        else{route_target_=routes_.target();ground_height_=route_target_.y;}
        return Reply{};
    }});
    result.emplace(464,Binding{0,2,[this](const Invocation& c){routes_.enqueue(std::bit_cast<int>(c.operands[0]),std::bit_cast<int>(c.operands[1]));return Reply{};}});
    result.emplace(465,Binding{0,0,[this](const Invocation&){routes_.clear_queue();return Reply{};}});
    for(unsigned opcode:{463u,488u,489u})result.emplace(opcode,Binding{0,1,[this,opcode](const Invocation& c){
        if(!c.state)throw std::runtime_error("Route query requires integer locals");
        const int queued=routes_.queued();
        int segment=routes_.segment();
        // The testing-room Exit/Hatch 3 handlers shortcut straight to their
        // transition on whole segments 5/24 and 8/9. Use their existing 484
        // path-search branch instead until the actual RS endpoint is reached.
        if(opcode==463&&exit_approach_&&c.program.name()=="entry_037.cb"&&
            (c.address==0xa00ec/4||c.address==0xa0190/4||c.address==0xa0614/4||c.address==0xa06b8/4))
            segment=exit_approach_arrived()?(exit_approach_->point==0?5:9):-1;
        c.state->integers[c.operands[0]]=opcode==463?segment:opcode==488?int(queued!=-1):queued==-1?-1:queued&0x7fff;
        return Reply{};
    }});
    const auto advance_route=[this]{
        // Original connection tie-break samples a timer. Host monotonic time
        // supplies the platform-dependent input to the same two-way choice.
        const auto timer=std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
        routes_.advance(((timer*1103515245u+12345u)>>16)&1);
        route_target_=routes_.target();ground_height_=route_target_.y;
    };
    result.emplace(456,Binding{0,0,[advance_route](const Invocation&){advance_route();return Reply{};}});
    result.emplace(466,Binding{0,0,[this,advance_route](const Invocation&){
        routes_.retreat();route_target_=routes_.target();ground_height_=route_target_.y;
        const auto delta=route_target_+actor_motion_.position*(-1);
        if(delta.x*delta.x+delta.z*delta.z<navigation_radius_squared_)advance_route();
        return Reply{};
    }});
    result.emplace(484,Binding{0,2,[this,advance_route](const Invocation& c){
        const bool reversed=routes_.search(std::bit_cast<int>(c.operands[0]),c.operands[1]!=0);
        route_target_=routes_.target();ground_height_=route_target_.y;
        if(reversed){const auto delta=route_target_+actor_motion_.position*(-1);if(delta.x*delta.x+delta.z*delta.z<navigation_radius_squared_)advance_route();}
        return Reply{};
    }});
    result.emplace(490,Binding{0,1,[this](const Invocation& c){routes_.search_limit(std::bit_cast<int>(c.operands[0]));return Reply{};}});
    result.emplace(521,Binding{0,0,[this](const Invocation&){const auto delta=route_target_+actor_motion_.position*(-1);return Reply{false,TransformMotion::angle(std::atan2(delta.x,delta.z)-actor_motion_.rotation.y)};}});
    result.emplace(368,Binding{0,0,[this](const Invocation& c){
        // These doorway workers can also be called by the route's broad
        // arrival segment. Complete the final approach before the worker is
        // allowed to disable speech and fade, not after changing scenes.
        if(c.program.name()=="entry_037.cb"&&
            (c.address==0x98210/4||c.address==0x9888c/4||c.address==0x98950/4)){
            const unsigned point=c.address==0x98210/4?0:34;
            const auto target=routes_.position(point);
            const auto delta=target+actor_motion_.position*(-1);
            if(math::dot_xyz(delta,delta)>16.f){
                // Ordinary swimming uses a ten-unit route arrival radius.
                // Doorway clearance needs a tighter, temporarily owned radius
                // or steering stops too early and Rio circles the doorway.
                if(!exit_approach_radius_)exit_approach_radius_=navigation_radius_squared_;
                navigation_radius_squared_=16.f;
                route_mode_=1; // The approach is swimming, so Stop uses the moving action.
                if(!navigation_target_||math::dot_xyz(*navigation_target_+target*(-1),*navigation_target_+target*(-1))>.01f){
                    navigation_origin_=actor_motion_.position;navigation_target_=target;ground_height_=target.y;navigation_turn_limit_=.1f;navigation_heading_offset_=0;
                    actor_motion_.stop_translation();actor_motion_.stop_turn(1);
                    actor_motion_.turn(0,0,.06f);actor_motion_.turn(2,0,.06f);
                }
                animate(94,1,.4f,.2f,{},false);
                return Reply{true,{}};
            }
            exit_approach_.reset();
            if(exit_approach_radius_){navigation_radius_squared_=*exit_approach_radius_;exit_approach_radius_.reset();}
            restore_actor_location(actor_motion_.position,actor_motion_.rotation,true,92);
        }
        automatic_commands_=actor_commands_enabled_=false;return Reply{};
    }});
    result.emplace(349,Binding{2,0,[this](const Invocation& c){get(c.arguments[0]).material_variant=unsigned(c.arguments[1]);return Reply{false,0.f};}});
    result.emplace(308,Binding{3,0,[this](const Invocation& c){
        const auto integer=[](float value){if(!std::isfinite(value) || double(value)<-2147483648. || double(value)>=2147483648.)throw std::runtime_error("Invalid node lighting argument");return std::int32_t(value);};
        auto& object=get(float(integer(c.arguments[0])));const int node=integer(c.arguments[1]),enabled=integer(c.arguments[2]);
        if(node<0)throw std::runtime_error("Invalid node lighting index");
        if(unsigned(node)<object.nodes.size())object.nodes[node].scene_lighting=enabled!=0;
        return Reply{false,float(enabled)};
    }});
    result.emplace(567,Binding{1,0,[this](const Invocation& c){const int index=std::int32_t(std::trunc(c.arguments[0]));lights_.groups(index<0?0xffffffff:0xffff8000u|(1u<<(index&31)));return Reply{false,float(index)};}});
    result.emplace(566,Binding{0,0,[this](const Invocation&){lights_.clear();return Reply{false,0.f};}});
    // 130d70/130e48/130f58: SLLV wraps each index to five bits, including negatives.
    // Unlike 567, -1 here selects bit 31 rather than enabling every group.
    for(unsigned count=2;count<=4;++count)result.emplace(586+count,Binding{count,0,[this](const Invocation& c){
        std::uint32_t mask=0;
        for(float value:c.arguments){
            if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid light group index");
            mask|=1u<<(std::uint32_t(std::int32_t(std::trunc(value)))&31);
        }
        lights_.groups(0xffff8000u|mask);
        return Reply{false,float(std::bit_cast<std::int32_t>(mask))};
    }});
    // 1311c0 consumes Z,Y,X and translates all active records, preserving W.
    result.emplace(587,Binding{3,0,[this](const Invocation& c){lights_.translate({c.arguments[2],c.arguments[1],c.arguments[0],0});return Reply{};}});
    for(unsigned opcode:{472u,473u,475u})result.emplace(opcode,Binding{opcode==475?5u:4u,0,[this,opcode](const Invocation& c){
        for(float v:c.arguments)if(!std::isfinite(v))throw std::runtime_error("Invalid actor navigation argument");
        if(c.arguments[3]<0)throw std::runtime_error("Invalid navigation turn limit");
        navigation_origin_=actor_motion_.position;navigation_target_=math::Vector4{c.arguments[0],c.arguments[1],c.arguments[2],1};ground_height_=navigation_target_->y;navigation_turn_limit_=c.arguments[3];navigation_heading_offset_=opcode==475?c.arguments[4]:0;return Reply{false,0.f};
    }});
    for(unsigned opcode:{474u,476u})result.emplace(opcode,Binding{opcode==474?1u:2u,0,[this,opcode](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid route navigation argument");
        if(c.arguments[0]<0)throw std::runtime_error("Invalid route navigation turn limit");
        navigation_origin_=actor_motion_.position;navigation_target_=route_target_;ground_height_=navigation_target_->y;navigation_turn_limit_=c.arguments[0];
        navigation_heading_offset_=opcode==476?c.arguments[1]:0;
        return Reply{false,opcode==474?c.arguments[0]:0.f};
    }});
    result.emplace(305,Binding{3,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[2]))throw std::runtime_error("Invalid node brightness");
        auto& node=get(c.arguments[0]).nodes.at(std::size_t(c.arguments[1]));node.brightness=c.arguments[2];node.tint={1,1,1,node.tint.w};return Reply{false,c.arguments[2]};
    }});
    result.emplace(306,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid node alpha parameter");
        if(std::trunc(c.arguments[1])<0)throw std::runtime_error("Negative node alpha index");
        auto& object=get(std::trunc(c.arguments[0]));const auto index=std::trunc(c.arguments[1]);
        if(index<float(object.nodes.size()))object.nodes[std::size_t(index)].tint.w=c.arguments[2];
        return Reply{false,c.arguments[2]};
    }});
    result.emplace(307,Binding{5,0,[this](const Invocation& c){
        for(unsigned i=2;i<5;++i)if(!std::isfinite(c.arguments[i]))throw std::runtime_error("Invalid node tint");
        auto& node=get(float(std::nearbyint(c.arguments[0]))).nodes.at(std::size_t(std::nearbyint(c.arguments[1])));
        node.brightness=1;node.tint={c.arguments[2],c.arguments[3],c.arguments[4],node.tint.w};return Reply{false,0.f};
    }});
    for(bool rotation:{false,true})for(unsigned axis=0;axis<3;++axis)result.emplace((rotation?201:185)+axis,Binding{1,0,[this,axis,rotation](const Invocation& c){
        // 116e30/116eb8/116f40 translation; 117ad0/117b58/117be0 rotation.
        const auto& transform=get(float(std::nearbyint(c.arguments[0]))).transform;
        const auto& position=rotation?transform.rotation:transform.position;
        return Reply{false,axis==0?position.x:axis==1?position.y:position.z};
    }});
    result.emplace(301,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>=2147483648.)throw std::runtime_error("Invalid node state argument");
        auto& object=get(float(std::nearbyint(c.arguments[0])));const auto index=int(std::nearbyint(c.arguments[1]));
        if(index<0)throw std::runtime_error("Negative node state index");
        // 2b0f20..2b0f28 ignores indices past the allocated node pool.
        // The tutorial applies the same setup to differently sized models.
        if(unsigned(index)>=object.nodes.size())return Reply{false,0.f};
        auto& node=object.nodes[unsigned(index)];
        node.state=node.source>=0?int(c.arguments[2]):-1;node.visible=node.state>=0;return Reply{false,0.f};
    }});
    for(unsigned opcode:{303u,351u,352u,353u})result.emplace(opcode,Binding{opcode==352?5u:opcode==353?4u:3u,0,[this,opcode](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>=2147483648.)throw std::runtime_error("Invalid node rendering argument");
        auto& object=get(std::nearbyint(c.arguments[0]));const int index=int(std::nearbyint(c.arguments[1]));
        if(index<0)throw std::runtime_error("Negative node rendering index");
        const float value=std::nearbyint(c.arguments[2]);
        if(unsigned(index)<object.nodes.size()){
            auto& node=object.nodes[unsigned(index)];
            if(opcode==303)node.rotation_order=int(value);
            else if(opcode==351)node.fog_mode=int(value);
            else if(opcode==352)node.fog_color={value,std::nearbyint(c.arguments[3]),std::nearbyint(c.arguments[4]),0};
            else {node.fog_near=c.arguments[2];node.fog_far=c.arguments[3];}
        }
        return Reply{false,opcode==303||opcode==351?value:0.f};
    }});
    for(unsigned opcode:{302u,304u})result.emplace(opcode,Binding{5,0,[this,opcode](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Nonfinite node transform argument");
        const auto index=std::nearbyint(c.arguments[1]);
        if(index<0||double(index)>=2147483648.)throw std::runtime_error("Invalid node transform index");
        auto& object=get(std::nearbyint(c.arguments[0]));
        // Original 2b0dd0..2b0de0 / 2b0e84..2b0e98 return when the
        // pool is absent or the requested node is past its allocated end.
        // Shared room setup can legitimately target a larger model variant.
        if(double(index)>=object.nodes.size())return Reply{false,0.f};
        auto& node=object.nodes[std::size_t(index)];
        const math::Vector4 value{c.arguments[2],c.arguments[3],c.arguments[4],opcode==304?1.f:0.f};
        if(opcode==302){node.rotation=value*(2*std::numbers::pi_v<float>);node.basis_sign={1,1,1,0};}else{
            node.position=value;if(std::size_t(index)<object.motion.size())object.motion[std::size_t(index)].controller.position=value;
        }return Reply{false,0.f};
    }});
    result.emplace(310,Binding{8,0,[this](const Invocation& c){
        // 11ef80 -> 1d8080: origin and extent for the node's culling box.
        // Rendering uses conservative native plane tests on its eight corners.
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Nonfinite node culling box");
        auto& object=get(std::nearbyint(c.arguments[0]));const auto index=std::nearbyint(c.arguments[1]);
        if(index<0||double(index)>=object.nodes.size())throw std::runtime_error("Node culling box index outside pool");
        object.nodes[std::size_t(index)].culling_box=std::array<math::Vector4,2>{math::Vector4{c.arguments[2],c.arguments[3],c.arguments[4],1},math::Vector4{c.arguments[5],c.arguments[6],c.arguments[7],0}};
        return Reply{false,0.f};
    }});
    result.emplace(309,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value) || double(value)<-2147483648. || double(value)>=2147483648.)throw std::runtime_error("Invalid node axis flip");
        auto& object=get(std::trunc(c.arguments[0]));const int index=int(c.arguments[1]),axis=int(c.arguments[2]);
        if(index<0)throw std::runtime_error("Negative node axis flip index");
        if(unsigned(index)<object.nodes.size() && axis>=0 && axis<3){auto& sign=object.nodes[index].basis_sign;
            if(axis==0)sign.x=-sign.x;else if(axis==1)sign.y=-sign.y;else sign.z=-sign.z;
        }
        return Reply{false,float(axis)};
    }});
    result.emplace(250,Binding{0,6,[this](const Invocation& c){
        auto& object=get(float(c.operands[0]));auto& motion=object.motion.at(c.operands[1]);
        motion.controller.position=object.nodes.at(c.operands[1]).position;
        motion.controller.move_to({std::bit_cast<float>(c.operands[2]),std::bit_cast<float>(c.operands[3]),std::bit_cast<float>(c.operands[4]),1},std::bit_cast<float>(c.operands[5]));return Reply{};
    }});
    result.emplace(273,Binding{0,2,[this](const Invocation& c){return Reply{get(float(c.operands[0])).motion.at(c.operands[1]).controller.moving(),{}};}});
    for(const auto opcode:{402u,403u})result.emplace(opcode,Binding{0,opcode==402?3u:4u,[this,opcode](const Invocation& c){
        animate(c.operands[0],c.operands[1],std::bit_cast<float>(c.operands[2]),opcode==403?std::bit_cast<float>(c.operands[3]):-1.f);return Reply{};
    }});
    result.emplace(405,Binding{0,5,[this](const Invocation& c){
        animate(c.operands[0],c.operands[1],std::bit_cast<float>(c.operands[2]),std::bit_cast<float>(c.operands[4]),std::bit_cast<float>(c.operands[3]));return Reply{};
    }});
    result.emplace(406,Binding{4,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[0]) || c.arguments[0]<0 || double(c.arguments[0])>=2147483648.0 || !std::isfinite(c.arguments[1]) || c.arguments[1]<0 || c.arguments[1]>=2)throw std::runtime_error("Invalid animation identifier or mode");
        return Reply{false,float(animate(unsigned(c.arguments[0]),unsigned(c.arguments[1]),c.arguments[2],-1.f,c.arguments[3]))};
    }});
    result.emplace(412,Binding{0,1,[this](const Invocation& c){if(!actor_cursor_)throw std::runtime_error("Actor has no animation controller");actor_cursor_->mode(c.operands[0]);return Reply{};}});
    for(unsigned opcode:{409u,410u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const float value=c.arguments[0];if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid actor root policy");
        const bool enabled=std::nearbyint(value)!=0;
        if(opcode==409)displacement_suppressed_=enabled;else root_pose_disabled_=enabled;
        return Reply{false,0.f};
    }});
    for(unsigned opcode:{319u,320u})result.emplace(opcode,Binding{0,5,[this,opcode](const Invocation& c){
        if(!c.state)throw std::runtime_error("Node query requires script locals");
        const auto& node=get(float(c.operands[0])).nodes.at(c.operands[1]);
        // 2b0d88 returns translation at +3e0; 2b0e38 returns the
        // Euler angles stored by 1e17c0 at +20, in turns (1cfcb0
        // multiplies them by 65536 for the original trig lookup). Native
        // nodes use radians internally; script read/modify/write must round
        // trip through the same units accepted by operation 302.
        const auto value=opcode==319?node.position:node.rotation*(1.f/(2*std::numbers::pi_v<float>));
        c.state->floats[c.operands[2]]=value.x;c.state->floats[c.operands[3]]=value.y;c.state->floats[c.operands[4]]=value.z;
        return Reply{};
    }});
    for(unsigned opcode:{407u,408u})result.emplace(opcode,Binding{0,opcode==407?3u:4u,[this,opcode](const Invocation& c){
        animate(c.operands[0],c.operands[1],std::bit_cast<float>(c.operands[2]),opcode==408?std::bit_cast<float>(c.operands[3]):-1.f,{},true);
        const auto& selected=*actor_playing_clip_;const float frame=actor_cursor_->position();const int i=int(frame);
        const auto origin=selected.frame(i).root_motion*(1-(frame-i))+selected.frame(i+1).root_motion*(frame-i)+selected.root_origin;
        actor_motion_.position={origin.x,0,origin.z,1};ground_height_=0.f;return Reply{};
    }});
    result.emplace(414,Binding{0,0,[this](const Invocation&){return Reply{false,actor_cursor_ && actor_cursor_->playing()?0.f:1.f};}});
    result.emplace(415,Binding{0,1,[this](const Invocation& c){
        if(!actor_cursor_)throw std::runtime_error("Animation threshold wait requires an active cursor");
        const float margin=std::bit_cast<float>(c.operands[0]);if(!std::isfinite(margin))throw std::runtime_error("Non-finite animation threshold");
        const float frame=std::trunc(actor_cursor_->position()),last=float(actor_playing_clip_->frames.size()-1);
        const bool reached=actor_cursor_->speed()>=0?frame>=last-margin:frame<=margin;
        return Reply{!reached,{}};
    }});
    result.emplace(411,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Animation query requires script state");
        c.state->integers[c.operands[0]]=actor_cursor_?std::int32_t(actor_clip_):0;return Reply{};
    }});
    result.emplace(413,Binding{0,1,[this](const Invocation& c){if(!actor_cursor_)throw std::runtime_error("Actor has no active animation");actor_cursor_->speed(std::bit_cast<float>(c.operands[0]));return Reply{};}});
    result.emplace(416,Binding{0,0,[this](const Invocation&){
        if(actor_cursor_ && actor_cursor_->playing()){++animation_wait_;return Reply{true,{}};}
        const float waited=float(animation_wait_);animation_wait_=0;return Reply{false,waited};
    }});
    result.emplace(420,Binding{0,1,[this](const Invocation& c){actor_visible_=c.operands[0]!=0;return Reply{};}});
    result.emplace(485,Binding{0,1,[this](const Invocation& c){actor_command_=std::bit_cast<std::int32_t>(c.operands[0]);return Reply{};}});
    result.emplace(505,Binding{0,0,[this](const Invocation&){if(!camera_eye_)throw std::runtime_error("Actor gaze requires a camera");gaze_.target(camera_eye_());return Reply{};}});
    result.emplace(506,Binding{1,0,[this](const Invocation& c){
        const float id=std::trunc(c.arguments[0]);
        // 128808 reads the object's transform-controller position once.
        // Later object motion does not retarget the actor automatically.
        gaze_.target(get(id).transform.position);return Reply{false,id};
    }});
    result.emplace(534,Binding{0,0,[this](const Invocation&){
        if(gaze_.head_mode()==0){++gaze_wait_;return Reply{true,{}};}
        const float waited=float(gaze_wait_);gaze_wait_=0;return Reply{false,waited};
    }});
    result.emplace(520,Binding{1,0,[this](const Invocation& c){gaze_.speed(c.arguments[0]);return Reply{false,0.f};}});
    result.emplace(536,Binding{3,0,[this](const Invocation& c){
        // 128cc8 -> 1dc408: change the SKICOM aiming axes, not the
        // actor's transform, animation angles, or world-space gaze target.
        if(!actor_ || actor_->bones.size()<=11)throw std::runtime_error("Body aiming axes require an actor skeleton");
        math::Vector4 axis{c.arguments[0],c.arguments[1],c.arguments[2],0};
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid body aiming axis");
        axis=math::normalize_xyz(axis);
        actor_->bones[11].gaze_axis=axis;
        actor_->bones[10].gaze_axis=axis;
        return Reply{false,0.f};
    }});
    result.emplace(537,Binding{3,0,[this](const Invocation& c){
        // 12f4f8 writes the actor's material emission offset, not gaze angles.
        actor_material_bias_={c.arguments[0],c.arguments[1],c.arguments[2],0};return Reply{false,0.f};
    }});
    const auto set_gaze_modes=[this](int eyes,int head,int body){
        if(actor_ && actor_->facial)gaze_.modes(eyes,head,body);
        else gaze_.modes(0,0,0); // 1ded20: no facial skeleton clears mode bits.
    };
    result.emplace(507,Binding{0,1,[set_gaze_modes](const Invocation& c){
        switch(c.operands[0]){case 0:set_gaze_modes(0,0,0);break;case 1:set_gaze_modes(1,0,0);break;case 2:set_gaze_modes(1,1,0);break;case 3:set_gaze_modes(1,1,1);break;default:break;}return Reply{};
    }});
    result.emplace(508,Binding{3,0,[set_gaze_modes](const Invocation& c){
        std::array<int,3> modes;
        for(unsigned i=0;i<3;++i){const auto value=c.arguments[i];
            if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid gaze mode argument");
            modes[i]=int(value);
        }
        set_gaze_modes(modes[0],modes[1],modes[2]);return Reply{false,0.f};
    }});
    result.emplace(509,Binding{0,0,[this](const Invocation&){return Reply{false,float(actor_ && actor_->facial?gaze_.modes():0)};}});
    result.emplace(502,Binding{3,0,[this](const Invocation& c){gaze_.target({c.arguments[0],c.arguments[1],c.arguments[2],1});return Reply{false,0.f};}});
    result.emplace(533,Binding{0,0,[this](const Invocation&){auto target=route_target_;target.y+=std::bit_cast<float>(0x416b3333u);gaze_.target(target);return Reply{false,0.f};}});
    result.emplace(504,Binding{3,0,[this](const Invocation& c){gaze_.target({c.arguments[0],c.arguments[1],c.arguments[2],0},true);return Reply{false,0.f};}});
    result.emplace(516,Binding{1,0,[this](const Invocation& c){
        const float value=c.arguments[0];
        if(!std::isfinite(value) || value<0 || value>=16)throw std::runtime_error("Invalid footstep surface");
        footsteps_.surface=int(value);return Reply{false,float(footsteps_.surface)};
    }});
    result.emplace(517,Binding{0,0,[this](const Invocation&){return Reply{false,float(footsteps_.surface)};}});
    for(const auto opcode:{518u,519u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){footsteps_.enabled=opcode==518;return Reply{false,footsteps_.enabled?1.f:0.f};}});
    for(const auto opcode:{417u,418u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        actor_expression_enabled_=opcode==418;return Reply{};
    }});
    for(const auto opcode:{369u,370u,371u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value))throw std::runtime_error("Invalid actor position");
        auto& position=actor_motion_.position;
        if(opcode==369)position.x=value;else if(opcode==370){position.y=value;ground_height_=value;}else position.z=value;
        return Reply{false,value};
    },true});
    for(const auto opcode:{372u,373u,374u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const auto value=TransformMotion::angle(c.arguments[0]);auto& rotation=actor_motion_.rotation;
        if(opcode==372)rotation.x=value;else if(opcode==373)rotation.y=value;else rotation.z=value;
        return Reply{false,value};
    },true});
    for(const auto opcode:{421u,422u})result.emplace(opcode,Binding{0,1,[this](const Invocation& c){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        actor_motion_.move_to(route_target_,std::bit_cast<float>(c.operands[0]));ground_height_=route_target_.y;return Reply{};
    }});
    for(const auto opcode:{423u,424u})result.emplace(opcode,Binding{4,0,[this](const Invocation& c){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        const math::Vector4 target{c.arguments[0],c.arguments[1],c.arguments[2],1};actor_motion_.move_to(target,c.arguments[3]);ground_height_=target.y;return Reply{false,0.f};
    }});
    for(const auto opcode:{426u,427u})result.emplace(opcode,Binding{4,0,[this](const Invocation& c){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        const auto target=math::transform(actor_world(),{c.arguments[0],c.arguments[1],c.arguments[2],1});
        actor_motion_.move_to(target,c.arguments[3]);ground_height_=target.y;return Reply{false,0.f};
    }});
    result.emplace(428,Binding{0,0,[this](const Invocation&){actor_motion_.stop_translation();navigation_target_.reset();return Reply{};}});
    result.emplace(429,Binding{0,0,[this](const Invocation&){return Reply{actor_motion_.moving() || navigation_target_.has_value(),{}};}});
    result.emplace(431,Binding{0,1,[this](const Invocation& c){if(!c.state)throw std::runtime_error("Motion query requires script locals");c.state->integers[c.operands[0]]=actor_motion_.moving() || navigation_target_.has_value();return Reply{};}});
    result.emplace(432,Binding{0,2,[this](const Invocation& c){navigation_target_.reset();actor_motion_.turn(1,std::bit_cast<float>(c.operands[0]),std::bit_cast<float>(c.operands[1]));return Reply{};}});
    result.emplace(433,Binding{0,2,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Actor turn requires script state");
        navigation_target_.reset();actor_motion_.turn(1,TransformMotion::angle(c.state->floats[c.operands[0]]),std::bit_cast<float>(c.operands[1]));return Reply{};
    }});
    for(unsigned opcode:{434u,435u})result.emplace(opcode,Binding{0,4,[this,opcode](const Invocation& c){
        if(opcode==435 && !c.state)throw std::runtime_error("Point-facing turn requires script locals");
        const auto coordinate=[&](unsigned axis){return opcode==435?c.state->floats[c.operands[axis]]:std::bit_cast<float>(c.operands[axis]);};
        const float x=coordinate(0),y=coordinate(1),z=coordinate(2);
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::runtime_error("Invalid point-facing target");
        const auto& from=actor_motion_.position;
        navigation_target_.reset();actor_motion_.turn(1,std::atan2(x-from.x,z-from.z),std::bit_cast<float>(c.operands[3]));return Reply{};
    }});
    result.emplace(439,Binding{0,2,[this](const Invocation& c){
        const float target=TransformMotion::angle(actor_motion_.rotation.y+std::bit_cast<float>(c.operands[0]));
        navigation_target_.reset();actor_motion_.turn(1,target,std::bit_cast<float>(c.operands[1]));return Reply{};
    }});
    for(unsigned opcode:{436u,437u})result.emplace(opcode,Binding{0,1,[this,opcode](const Invocation& c){
        const auto delta=route_target_+actor_motion_.position*(-1);
        float heading=std::atan2(delta.x,delta.z);
        if(opcode==437)heading=TransformMotion::angle(heading+std::numbers::pi_v<float>);
        navigation_target_.reset();actor_motion_.turn(1,heading,std::bit_cast<float>(c.operands[0]));return Reply{};
    }});
    result.emplace(438,Binding{0,2,[this](const Invocation& c){
        const auto target=routes_.position(c.operands[0]);
        navigation_target_.reset();actor_motion_.turn(1,std::atan2(target.x-actor_motion_.position.x,target.z-actor_motion_.position.z),std::bit_cast<float>(c.operands[1]));return Reply{};
    }});
    result.emplace(483,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::runtime_error("Actor command gate query requires script locals");
        c.state->integers[c.operands[0]]=actor_commands_enabled_?1:0;return Reply{};
    }});
    result.emplace(440,Binding{2,0,[this](const Invocation& c){
        // 12bd18..12bd20 uses both wrappers' +80 simulation positions,
        // not the target's +f0 cylindrical render position.
        const auto id=slot(std::nearbyint(c.arguments[0]));const auto* target=instance(id);
        if(!target)throw std::runtime_error("Actor turn target is not loaded");
        const auto delta=target->transform.position+actor_motion_.position*(-1);
        navigation_target_.reset();actor_motion_.turn(1,std::atan2(delta.x,delta.z),c.arguments[1]);return Reply{false,float(id)};
    }});
    result.emplace(441,Binding{0,0,[this](const Invocation&){return Reply{actor_motion_.turning(1),{}};}});
    result.emplace(443,Binding{0,1,[this](const Invocation& c){actor_motion_.turn_gain(std::bit_cast<float>(c.operands[0]));return Reply{};}});
    // 12a660 -> 2b1958 selects active controller zero and returns its frame.
    // With the original subframe-query switch unset, CVT.W.S rounds it.
    result.emplace(529,Binding{0,0,[this](const Invocation&){
        if(!actor_cursor_)throw std::runtime_error("Animation frame query requires an active controller");
        return Reply{false,std::nearbyint(actor_cursor_->position())};
    }});
    result.emplace(531,Binding{0,0,[this](const Invocation&){
        // 12a6b0 -> 2b18f8 reads active controller +64, i.e. cursor +54:
        // the same speed added to its position by 1c5cf0..1c5cfc.
        if(!actor_cursor_)throw std::runtime_error("Animation speed query requires an active controller");
        return Reply{false,actor_cursor_->speed()};
    }});
    for(unsigned axis=0;axis<3;++axis)result.emplace(525+axis,Binding{0,0,[this,axis](const Invocation&){
        // 129680/1296d0/129720 read actor +f0, updated at 1d7884.
        // Unlike 381..383 this is the transformed cylindrical world position.
        const auto p=actor_world().columns[3];return Reply{false,axis==0?p.x:axis==1?p.y:p.z};
    }});
    for(unsigned axis=0;axis<3;++axis)result.emplace(340+axis,Binding{1,0,[this,axis](const Invocation& c){
        const auto p=get(std::nearbyint(c.arguments[0])).world_matrix().columns[3];
        return Reply{false,axis==0?p.x:axis==1?p.y:p.z};
    }});
    result.emplace(528,Binding{0,0,[this](const Invocation&){
        const auto height=[](const SkeletalClip& clip,float frame){return clip.frame(int(std::nearbyint(frame))).root_motion.y+clip.root_origin.y;};
        const float current=height(actor_cursor_?*actor_playing_clip_:clip(0),actor_cursor_?actor_cursor_->position():0.f);
        return Reply{false,previous_cursor_?height(*actor_prior_clip_,previous_cursor_->position())*(1-actor_blend_)+current*actor_blend_:current};
    }});
    result.emplace(530,Binding{1,0,[this](const Invocation& c){
        const float radius=c.arguments[0],squared=radius*radius;
        if(!std::isfinite(radius)||!std::isfinite(squared))throw std::runtime_error("Invalid navigation arrival radius");
        navigation_radius_squared_=squared;return Reply{false,0.f};
    }});
    result.emplace(442,Binding{0,0,[this](const Invocation&){if(!actor_)throw std::runtime_error("Actor skeleton is not available");actor_motion_.stop_turn(1);return Reply{};}});
    result.emplace(364,Binding{0,2,[this](const Invocation& c){
        if(!actor_)throw std::runtime_error("Actor skeleton is not available");
        if(c.operands[1]>=actor_banks_.size())throw std::runtime_error("Invalid actor animation bank index");
        const auto name=c.program.string(c.operands[0]);
        // 1c6150 -> 1c6e50 searches loaded archive slots in order. A miss
        // logs and returns -1 BEFORE changing either bank pointer. 128438
        // ignores that return value and advances past opcode 364.
        const auto* files=resources_.first_loaded_files(name);
        if(!files){std::cerr<<"Animation bank not loaded; retaining current bank: "<<name<<'\n';return Reply{};}
        if(files->size()!=1||files->front().conversion!="raw"||!files->front().bytes)
            throw std::runtime_error("Animation bank requires raw resource: "+name);
        auto bank=decode_animation_bank(*files->front().bytes);
        // 1d7678 creates an animation controller on first load (1c5fc8,
        // base bank). Existing controllers select bank a2 via 1c6150.
        const auto index=actor_banks_[0]?c.operands[1]:0u;
        actor_bank(index,std::move(bank));return Reply{};
    }});
    result.emplace(175,Binding{2,0,[this](const Invocation& c){
        const auto id=slot(c.arguments[0]);const auto address=c.arguments[1];
        if(!std::isfinite(address) || address<0 || std::floor(address)!=address || double(address)>=c.program.size())throw std::runtime_error("Invalid object name address");
        const auto name=c.program.string(std::uint32_t(address));
        auto replacement=std::make_unique<Instance>(resources_.find_raw(name),name);
        lights_.replace(replacement->model.lights);
        slots_[id]=std::move(replacement);return Reply{false,float(id)};
    }});
    result.emplace(239,Binding{2,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid object visibility");
        get(c.arguments[0]).visible=std::trunc(c.arguments[1])!=0;return Reply{false,0.f};
    }});
    // 119ba8 (239) directly calls 119bc8 (240); both set the drawable's
    // +100 enable flag through 2b12c8, consumed by 1d78d8.
    result.emplace(240,result.at(239));
    result.emplace(242,Binding{1,0,[this](const Invocation& c){
        // 1197c0 / 2b19d0: controller state 0 (idle) or 2 (finished).
        const auto& object=get(slot(std::nearbyint(c.arguments[0])));
        return Reply{false,object.cursor&&object.cursor->playing()?0.f:1.f};
    }});
    result.emplace(298,Binding{2,0,[this](const Invocation& c){
        const float count=std::trunc(c.arguments[1]);
        if(!std::isfinite(count) || count<0 || count>=512)throw std::runtime_error("Node pool count outside native drawable range 0..511");
        allocate_nodes(slot(std::trunc(c.arguments[0])),unsigned(count));return Reply{false,count};
    }});
    result.emplace(300,Binding{1,0,[this](const Invocation& c){
        auto& object=get(std::trunc(c.arguments[0]));
        // 2b0fa0 hides every allocated node and restores its color defaults.
        // It retains source assignments, local transforms and controllers.
        for(auto& node:object.nodes){node.state=-1;node.visible=false;node.brightness=1;node.tint={1,1,1,1};node.rotation_order=0;node.fog_mode=1;}
        return Reply{false,std::trunc(c.arguments[0])};
    }});
    result.emplace(299,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value) || value< -2147483648.f || value>=2147483648.f)throw std::runtime_error("Invalid geometry assignment argument");
        auto& object=get(std::trunc(c.arguments[0]));const int destination=int(c.arguments[1]),source=int(c.arguments[2]);
        if(destination<0)throw std::runtime_error("Negative geometry destination node");
        if(unsigned(destination)>=object.nodes.size())return Reply{false,float(source)};
        auto& node=object.nodes[destination];node.state=-1;node.visible=false;
        // An invalid or empty source hides the node without discarding its
        // previous geometry/material assignment (1e1e60).
        if(source>=0 && unsigned(source)<object.geometry_nodes.size() && object.geometry_nodes[source].present){
            const auto& geometry=object.geometry_nodes[source];node.source=source;
            node.fog_mode=1;node.fog_near=0;node.fog_far=50;node.fog_color={128,0,0,0};
            node.brightness=1;node.tint={geometry.tint.x,geometry.tint.y,geometry.tint.z,1};node.bind=geometry.bind;node.pivot=geometry.pivot;
        }
        return Reply{false,float(source)};
    }});
    result.emplace(327,Binding{1,0,[this](const Invocation& c){
        const unsigned id=slot(std::trunc(c.arguments[0]));auto& object=get(float(id));
        allocate_nodes(id,object.model.nodes);auto& nodes=object.nodes;
        for(unsigned i=0;i<nodes.size();++i){
            const auto& source=object.geometry_nodes.at(i);const bool geometry=source.present;
            if(geometry)nodes[i].source=int(i);
            // Original 2b0f10 sets state zero for valid nodes, -1 otherwise.
            nodes[i].visible=geometry;
            nodes[i].state=geometry?0:-1;
            nodes[i].pivot=source.pivot;nodes[i].bind=source.bind;
            // SOLCOM's fourth material float is zero in opaque room nodes;
            // it is not the renderer's multiplicative vertex alpha.
            if(geometry)nodes[i].tint={source.tint.x,source.tint.y,source.tint.z,1};
        }
        return Reply{false,float(object.nodes.size())};
    }});
    result.emplace(246,Binding{1,0,[this](const Invocation& c){
        auto& object=get(c.arguments[0]);
        // 119f98 selects per-node controllers only after node setup allocated
        // the node array; otherwise 11a0bc binds one whole-object controller.
        std::vector<Instance::Motion> motion(object.nodes_initialized?object.nodes.size():1);
        if(object.nodes_initialized)for(unsigned i=0;i<motion.size();++i)motion[i].node=i;
        object.motion=std::move(motion);return Reply{false,c.arguments[0]};
    }});
    result.emplace(330,Binding{8,0,[this](const Invocation& c){
        // 123398 -> 1dec30: final bone matrix for this update, not a new clip.
        // 1cfcb0 takes turns (65536 units), builds Rz*Ry*Rx, then sets XYZ.
        for(float v:c.arguments)if(!std::isfinite(v))throw std::runtime_error("Nonfinite bone override");
        for(unsigned i=0;i<2;++i)if(double(c.arguments[i])<-2147483648.||double(c.arguments[i])>=2147483648.)throw std::runtime_error("Bone override selector outside integer range");
        const int id=int(std::nearbyint(c.arguments[0])),bone=int(std::nearbyint(c.arguments[1]));
        auto matrix=math::identity();
        for(unsigned axis=0;axis<3;++axis){
            const auto turn=std::fmod(std::nearbyint(double(c.arguments[2+axis])*65536),65536.);
            TransformMotion rotation;const float angle=float(turn)*(2*std::numbers::pi_v<float>/65536);
            if(axis==0)rotation.rotation.x=angle;else if(axis==1)rotation.rotation.y=angle;else rotation.rotation.z=angle;
            matrix=math::multiply(rotation.matrix(),matrix);
        }
        matrix.columns[3]={c.arguments[5],c.arguments[6],c.arguments[7],1};
        if(id==-1){
            if(!actor_)throw std::runtime_error("Bone override requires actor skeleton");
            if(bone>=0&&unsigned(bone)<actor_->bones.size()){
                actor_pose_overrides_[unsigned(bone)]=matrix;
                if(unsigned(bone)<current_pose_.size())current_pose_[unsigned(bone)]=math::multiply(actor_world(),matrix);
            }
        }else{
            if(id<0||id>=32)throw std::runtime_error("Bone override object outside slot table");
            if(auto* object=slots_[unsigned(id)].get();object&&object->skeleton&&bone>=0&&unsigned(bone)<object->skeleton->bones.size()){
                object->pose_overrides[unsigned(bone)]=matrix;
                object->skeletal_pose.at(unsigned(bone))=matrix;
                if(object->skin)pose_skin_surface(object->model,*object->skin,*object->skeleton,object->skeletal_pose);
            }
        }
        return Reply{false,0.f};
    }});
    result.emplace(329,Binding{4,0,[this](const Invocation& c){
        auto& child=get(c.arguments[0]);
        for(unsigned i=1;i<4;++i)if(!std::isfinite(c.arguments[i]) || std::trunc(c.arguments[i])!=c.arguments[i] || c.arguments[i]< -32768 || c.arguments[i]>32767)throw std::runtime_error("Invalid object attachment argument");
        const int parent=int(c.arguments[1]),bone=int(c.arguments[2]),first=int(c.arguments[3]);
        if(!child.skeleton)throw std::runtime_error("Attachment child is not skeletal");
        child.attachment.reset();
        if(bone<=0)return Reply{false,0.f};
        const Skeleton* skeleton=nullptr;
        if(parent==-1){if(actor_)skeleton=&*actor_;}
        else {auto& object=get(float(parent));if(object.skeleton)skeleton=&*object.skeleton;}
        if(!skeleton)throw std::runtime_error("Attachment parent skeleton is not available");
        if(unsigned(bone)>=skeleton->bones.size() || first<0)throw std::runtime_error("Attachment bone/channel is outside parent skeleton");
        child.attachment=Instance::Attachment{parent,unsigned(bone),unsigned(first)};
        return Reply{false,0.f};
    }});
    result.merge(animation_bindings());result.merge(model_animation_bindings());result.merge(object_motion_bindings());result.merge(object_attachment_bindings());return result;
}
}
