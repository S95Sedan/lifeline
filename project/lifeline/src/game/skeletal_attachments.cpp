#include "game/objects.hpp"

namespace lifeline::script {
void Objects::advance_skeletal_attachments(std::span<const math::Matrix4> pose){
    // Binding 329 attaches an entire animated skin. Its channels come from
    // the parent's controller, including the outgoing clip during a blend.
    std::array<unsigned char,32> visited{};
    const auto update=[&](auto&& self,unsigned id)->void{
        auto* child=slots_[id].get();if(!child || !child->attachment || visited[id]==2)return;
        if(visited[id]==1)throw std::runtime_error("Cyclic skeletal attachment");
        visited[id]=1;const auto link=*child->attachment;
        const SkeletalClip* current=nullptr;const SkeletalClip* previous=nullptr;
        float frame=0,prior_frame=0,weight=1;std::vector<math::Matrix4> parent_pose;
        if(link.parent==-1){
            parent_pose.assign(pose.begin(),pose.end());
            if(actor_cursor_){current=actor_playing_clip_.get();frame=actor_cursor_->position();
                if(previous_cursor_){previous=actor_prior_clip_.get();prior_frame=previous_cursor_->position();weight=actor_blend_;}}
        }else{
            self(self,unsigned(link.parent));const auto& parent=get(float(link.parent));
            parent_pose=parent.skeletal_pose;const auto world=parent.world_matrix();
            for(auto& bone:parent_pose)bone=math::multiply(world,bone);
            if(parent.cursor){current=parent.playing_clip.get();frame=parent.cursor->position();
                if(parent.previous_cursor){previous=parent.prior_clip.get();prior_frame=parent.previous_cursor->position();weight=parent.blend;}}
            else if(parent.animations)current=&parent.animations->clips.front();
        }
        if(current && !parent_pose.empty()){
            if(link.bone>=parent_pose.size())throw std::runtime_error("Attachment parent pose lacks target bone");
            auto child_pose=previous?blend_attachment_skeleton(*child->skeleton,*previous,prior_frame,*current,frame,weight,link.first_channel):sample_attachment_skeleton(*child->skeleton,*current,frame,link.first_channel);
            for(auto& bone:child_pose)bone=math::multiply(parent_pose[link.bone],bone);
            child->skeletal_pose=std::move(child_pose);
            pose_skin_surface(child->model,*child->skin,*child->skeleton,child->skeletal_pose);
            // The attached pose already contains its parent world transform.
            child->attached_world=math::identity();
        }
        visited[id]=2;
    };
    for(unsigned id=0;id<slots_.size();++id)update(update,id);
}
}
