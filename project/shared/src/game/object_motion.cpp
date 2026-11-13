#include "game/objects.hpp"
#include <bit>
#include <cmath>
#include <numbers>

namespace lifeline::script {
Bindings Objects::object_motion_bindings(){
    Bindings result;
    constexpr float node_turn=2*std::numbers::pi_v<float>;
    result.emplace(276,Binding{2,0,[this](const Invocation& c){
        auto& object=get(std::nearbyint(c.arguments[0]));const float index=std::nearbyint(c.arguments[1]);
        if(!std::isfinite(index)||index<0||index>=object.motion.size())throw std::runtime_error("Node velocity wait outside motion table");
        auto& motion=object.motion[unsigned(index)];auto& controller=motion.node?motion.controller:object.transform;
        if(!controller.velocity_near_zero())return Reply{true,{}};
        controller.stop_translation();return Reply{false,0.f};
    }});
    result.emplace(287,Binding{3,0,[this](const Invocation& c){
        auto& object=get(std::nearbyint(c.arguments[0]));
        if(object.motion.empty())object.motion.resize(1);
        object.transform.turn(1,c.arguments[1],c.arguments[2]);return Reply{false,c.arguments[1]};
    }});
    result.emplace(267,Binding{8,0,[this](const Invocation& c){
        auto& object=get(std::nearbyint(c.arguments[0]));const float index=std::nearbyint(c.arguments[1]);
        if(!std::isfinite(index)||index<0||index>=object.nodes.size()||index>=object.motion.size())throw std::runtime_error("Node trajectory outside motion table");
        const auto node=unsigned(index);const auto matrix=object.nodes[node].local_matrix();
        const auto velocity=math::transform(matrix,{c.arguments[2],c.arguments[3],c.arguments[4],0});
        const auto acceleration=math::transform(matrix,{c.arguments[5],c.arguments[6],c.arguments[7],0});
        object.motion[node].controller.trajectory(velocity,acceleration);return Reply{false,0.f};
    }});
    result.emplace(283,Binding{5,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid object facing target");
        auto& object=get(float(std::nearbyint(c.arguments[0])));
        if(object.motion.empty())object.motion.resize(1);
        const auto& position=object.transform.position;
        // 11d4b4..11d504 computes the target's X/Z bearing and turns axis 1.
        object.transform.turn(1,std::atan2(c.arguments[1]-position.x,c.arguments[3]-position.z),c.arguments[4]);
        return Reply{false,0.f};
    }});
    result.emplace(514,Binding{0,1,[this](const Invocation& c){
        actor_motion_.turn_acceleration(c.operands[0]!=0);return Reply{};
    }});
    for(unsigned opcode=251;opcode<=256;++opcode)result.emplace(opcode,Binding{0,(opcode&1)?3u:4u,[this,opcode](const Invocation& c){
        auto& object=get(float(c.operands[0]));const bool node_command=(opcode&1)==0;
        const unsigned index=node_command?c.operands[1]:0;
        if(!node_command && object.motion.empty())object.motion.resize(1);
        auto& motion=object.motion.at(index);auto& controller=motion.node?motion.controller:object.transform;
        auto target=node_command?object.nodes.at(index).position:object.transform.position;
        const float coordinate=std::bit_cast<float>(c.operands[node_command?2:1]);
        switch((opcode-251)/2){case 0:target.x=coordinate;break;case 1:target.y=coordinate;break;default:target.z=coordinate;break;}
        controller.move_to(target,std::bit_cast<float>(c.operands[node_command?3:2]));return Reply{};
    }});
    result.emplace(260,Binding{0,5,[this](const Invocation& c){
        auto& object=get(float(c.operands[0]));
        if(object.motion.empty())object.motion.resize(1);
        auto& motion=object.motion.front();auto& controller=motion.node?motion.controller:object.transform;
        const math::Vector4 offset{std::bit_cast<float>(c.operands[1]),std::bit_cast<float>(c.operands[2]),std::bit_cast<float>(c.operands[3]),1};
        controller.move_to(math::transform(object.transform.matrix(),offset),std::bit_cast<float>(c.operands[4]));return Reply{};
    }});
    for(unsigned opcode:{268u,269u,270u})result.emplace(opcode,Binding{opcode==270?2u:1u,0,[this,opcode](const Invocation& c){
        const float id=std::trunc(c.arguments[0]);auto& object=get(id);
        if(object.motion.empty())return Reply{false,0.f};
        const float index=opcode==270?std::trunc(c.arguments[1]):0.f;
        if(!std::isfinite(index)||index<0||index>=object.motion.size())throw std::runtime_error("Object motion controller outside allocated range");
        auto& motion=object.motion[std::size_t(index)];
        (motion.node?motion.controller:object.transform).stop_translation();return Reply{false,0.f};
    }});
    result.emplace(271,Binding{1,0,[this](const Invocation& c){
        const auto& object=get(std::trunc(c.arguments[0]));
        if(object.motion.empty())return Reply{false,0.f};
        const auto& motion=object.motion.front();
        return Reply{false,(motion.node?motion.controller:object.transform).moving()?1.f:0.f};
    }});
    result.emplace(272,Binding{0,1,[this](const Invocation& c){
        const auto& object=get(float(c.operands[0]));if(object.motion.empty())return Reply{};
        const auto& motion=object.motion.front();return Reply{(motion.node?motion.controller:object.transform).moving(),{}};
    }});
    result.emplace(261,Binding{0,6,[this](const Invocation& c){
        auto& object=get(float(c.operands[0]));const auto index=c.operands[1];
        const auto& node=object.nodes.at(index);auto& motion=object.motion.at(index);
        const math::Vector4 offset{std::bit_cast<float>(c.operands[2]),std::bit_cast<float>(c.operands[3]),std::bit_cast<float>(c.operands[4]),1};
        // 11b6bc uses the node's live local matrix (+3b0), before the
        // containing model's world transform or authored geometry bind.
        motion.controller.move_to(math::transform(node.local_matrix(),offset),std::bit_cast<float>(c.operands[5]));return Reply{};
    }});
    result.emplace(511,Binding{3,0,[this](const Invocation& c){
        // 101c60 aliases process +10 to +0c: both address the worker's
        // argument stack. 12b630 pops axis, maximum step, then target.
        const float axis=std::trunc(c.arguments[2]);
        if(!std::isfinite(axis)||axis<0||axis>2)throw std::runtime_error("Actor rotation axis outside 0..2");
        navigation_target_.reset();actor_motion_.turn(unsigned(axis),c.arguments[0],c.arguments[1]);return Reply{false,0.f};
    }});
    for(unsigned opcode:{512u,513u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const float value=c.arguments[0];if(!std::isfinite(value)||std::trunc(value)<0||std::trunc(value)>2)throw std::runtime_error("Actor rotation axis outside 0..2");
        const auto axis=unsigned(std::trunc(value));
        if(opcode==512)return Reply{actor_motion_.turning(axis),0.f};
        actor_motion_.stop_turn(axis);return Reply{false,0.f};
    }});
    result.emplace(522,Binding{3,0,[this](const Invocation& c){
        // 12bed0 discards the middle (height) expression without using it.
        const float x=c.arguments[0],z=c.arguments[2];
        if(!std::isfinite(x)||!std::isfinite(z))throw std::runtime_error("Invalid actor heading target");
        const auto& p=actor_motion_.position;
        const float heading=std::atan2(x-p.x,z-p.z);
        return Reply{false,TransformMotion::angle(heading-actor_motion_.rotation.y)};
    }});
    result.emplace(532,Binding{0,0,[this](const Invocation&){actor_motion_.clear_turn_velocity();return Reply{false,0.f};}});
    result.emplace(398,Binding{0,1,[this](const Invocation& c){if(actor_)actor_cylindrical_=c.operands[0]!=0;return Reply{};}});
    for(unsigned opcode:{280u,281u,290u})result.emplace(opcode,Binding{0,4,[this,opcode](const Invocation& c){
        auto& object=get(float(c.operands[0]));auto& motion=object.motion.at(c.operands[1]);
        auto& controller=motion.node?motion.controller:object.transform;
        if(motion.node)controller.rotation=object.nodes.at(*motion.node).rotation;
        float target;
        if(opcode==281){
            if(!c.state)throw std::runtime_error("Node rotation requires float locals");
            target=TransformMotion::angle(c.state->floats[c.operands[2]]*node_turn);
        }else{
            target=std::bit_cast<float>(c.operands[2])*node_turn;
            if(opcode==290)target+=controller.rotation.y;
        }
        // Node Euler fields are turns: 1e17c0 stores them and multiplies by
        // 65536 before the trigonometric lookup. The native controller and
        // renderer use radians, so convert both target and maximum step.
        controller.turn(1,TransformMotion::angle(target),std::bit_cast<float>(c.operands[3])*node_turn);return Reply{};
    }});
    result.emplace(289,Binding{0,3,[this](const Invocation& c){
        auto& controller=get(float(c.operands[0])).transform;
        controller.turn(1,TransformMotion::angle(controller.rotation.y+std::bit_cast<float>(c.operands[1])),std::bit_cast<float>(c.operands[2]));return Reply{};
    }});
    result.emplace(292,Binding{1,0,[this](const Invocation& c){
        const float value=c.arguments[0];if(!std::isfinite(value))throw std::runtime_error("Invalid rotation stop object");
        get(std::trunc(value)).transform.stop_turn(1);return Reply{false,0.f};
    }});
    result.emplace(291,Binding{2,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[0]))throw std::runtime_error("Invalid actor-facing object");
        auto& controller=get(std::trunc(c.arguments[0])).transform;
        const auto delta=actor_motion_.position+controller.position*(-1);
        // 1d5110 supplies destination-minus-source X/Z to atan2.
        const float heading=std::atan2(delta.x,delta.z);
        controller.turn(1,heading,c.arguments[1]);return Reply{false,heading};
    }});
    result.emplace(293,Binding{0,1,[this](const Invocation& c){return Reply{get(float(c.operands[0])).transform.turning(1),{}};}});
    result.emplace(294,Binding{0,2,[this](const Invocation& c){
        const auto& object=get(float(c.operands[0]));const auto& motion=object.motion.at(c.operands[1]);
        return Reply{(motion.node?motion.controller:object.transform).turning(1),{}};
    }});
    return result;
}
}
