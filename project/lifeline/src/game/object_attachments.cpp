#include "game/objects.hpp"
#include <bit>
#include <cmath>

namespace lifeline::script {
Bindings Objects::object_attachment_bindings(){
    Bindings result;
    for(unsigned opcode:{323u,324u})result.emplace(opcode,Binding{0,opcode==323?8u:7u,[this,opcode](const Invocation& c){
        const auto source=slot(float(c.operands[0])),index=c.operands[1];
        if(index>=8)throw std::runtime_error("Object attachment entry outside 0..7");
        const int destination=std::bit_cast<int>(c.operands[2]);
        if(destination>=0)slot(float(destination));
        const int node=opcode==323?std::bit_cast<int>(c.operands[3]):-1;
        const unsigned bone=c.operands[opcode==323?4:3],offset=opcode==323?5:4;
        const math::Vector4 position{std::bit_cast<float>(c.operands[offset]),std::bit_cast<float>(c.operands[offset+1]),std::bit_cast<float>(c.operands[offset+2]),1};
        if(bone>32767||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))throw std::runtime_error("Invalid object attachment bone or offset");
        if(opcode==323 && destination>=0 && node>=0){
            auto& target=get(float(destination)).nodes.at(unsigned(node));target.rotation={0,0,0,0};target.basis_sign={1,1,1,0};target.state=target.source>=0?1:-1;target.visible=target.state>=0;
        }
        object_attachments_[source][index]={destination,int(bone),node,position};return Reply{};
    }});
    result.emplace(325,Binding{0,2,[this](const Invocation& c){object_attachments_[slot(float(c.operands[0]))].at(c.operands[1])={};return Reply{};}});
    result.emplace(326,Binding{0,1,[this](const Invocation& c){object_attachments_[slot(float(c.operands[0]))].fill({});return Reply{};}});
    return result;
}
void Objects::apply_object_attachments(){
    // 1206e0 walks eight records for each source model. Node targets use the
    // recorded offset; whole-model targets retain their own TRS properties.
    for(unsigned source=0;source<slots_.size();++source){
        if(!slots_[source])continue;
        for(const auto& link:object_attachments_[source]){
            if(link.object<0 || !slots_.at(unsigned(link.object)))continue;
            const auto& parent=*slots_[source];
            if(link.bone<0||unsigned(link.bone)>=parent.skeletal_pose.size())throw std::runtime_error("Object attachment bone outside current pose");
            auto& target=*slots_[unsigned(link.object)];auto* node=link.node<0?nullptr:&target.nodes.at(unsigned(link.node));
            const auto angles=node?node->rotation:target.transform.rotation;
            TransformMotion x,y,z;x.rotation.x=angles.x;y.rotation.y=angles.y;z.rotation.z=angles.z;
            auto local=math::multiply(z.matrix(),math::multiply(y.matrix(),x.matrix()));
            if(!node){local.columns[0]=local.columns[0]*target.scale.x;local.columns[1]=local.columns[1]*target.scale.y;local.columns[2]=local.columns[2]*target.scale.z;}
            local.columns[3]=node?link.offset:target.transform.position;local.columns[3].w=1;
            const auto bone=math::multiply(parent.world_matrix(),parent.skeletal_pose[unsigned(link.bone)]);
            const auto world=math::multiply(bone,local);
            if(node)node->attached_world=world;else target.attached_world=world;
        }
    }
}
}
