#include "game/effects/effect_attachments.hpp"
#include "rendering/model.hpp"

namespace lifeline::script {
namespace {
math::Matrix4 camera_attachment(const SceneCamera& camera,int part){
    // 18d898's selectors 3 and 5 both return camera +f0. 1ca740 builds that
    // matrix from the camera's right, corrected-up and forward axes, followed
    // by its eye position. Effects use it to turn camera-relative authored
    // vectors into world-space vectors.
    if(part!=3 && part!=5)throw std::runtime_error("Camera effect attachment selector outside recovered range");
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1.f);
    const auto right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(forward,right);
    return {{right,up,forward,camera.eye}};
}
}

math::Matrix4 effect_attachment(const Objects& objects,int source,int part){
    if(source==0 && part==0)return math::identity();
    // 18d940's actor selectors address particular bones, not a linear index.
    if(source==2){
        if(part==0)return objects.actor_world();
        constexpr unsigned bones[]{16,20,12,15,19,14,18,2,6,3,7,4,8,1,10,11,62,63};
        if(part<1 || unsigned(part)>std::size(bones))throw std::runtime_error("Actor effect attachment selector outside recovered range");
        const auto bone=bones[part-1];const auto& pose=objects.current_pose();
        if(bone>=pose.size())throw std::runtime_error("Actor effect attachment pose is unavailable");return pose[bone];
    }
    if(source>=3 && source<35){
        const auto* object=objects.instance(unsigned(source-3));if(!object)throw std::runtime_error("Effect attachment object is unavailable");
        if(part==0)return object->world_matrix();
        // 3093a0 maps common authored attachment names; selectors >=73 directly
        // address skeleton bones after subtracting 73 (18e508).
        constexpr unsigned bones[]{27,14,23,16,9,4,1,3,6,18,21,7,6,2,43,49,52,53,2,13,27,9,5,20,16,2,12,10,6,1,15,26,38,2,5,7,3,31,10,19,27,9,48,45,3,4,6,5,52,16,34,5,15,33,14,32,50,53,51,54,52,55,2,2,2,6,58,61,64,52,55,6};
        if(part<0)throw std::runtime_error("Negative object effect attachment selector");
        const auto bone=part>=73?unsigned(part-73):bones[part-1];
        if(bone>=object->skeletal_pose.size())throw std::runtime_error("Object effect attachment bone is unavailable");
        return math::multiply(object->world_matrix(),object->skeletal_pose[bone]);
    }
    if(source>=35 && source<67){
        const auto* object=objects.instance(unsigned(source-35));
        if(!object || part<0 || unsigned(part)>=object->nodes.size())throw std::runtime_error("Effect attachment node is unavailable");
        // 18e540 returns SOLCOM node +3b0, the resolved world matrix, not
        // its local animation fields. Attached props (the air shooter) must
        // emit from the same matrix used to draw their node.
        const auto& node=object->nodes[unsigned(part)];
        if(node.attached_world)return *node.attached_world;
        const int mode=node.state&7;
        const auto local=mode==1?node.local_matrix():mode==2?
            math::multiply(node.bind,node.local_matrix()):node.bind;
        return math::multiply(object->world_matrix(),local);
    }
    throw std::runtime_error("Effect attachment source not recovered: "+std::to_string(source));
}

math::Matrix4 effect_attachment(const Objects& objects,const SceneCamera& camera,int source,int part){
    if(source==1)return camera_attachment(camera,part);
    return effect_attachment(objects,source,part);
}
}
