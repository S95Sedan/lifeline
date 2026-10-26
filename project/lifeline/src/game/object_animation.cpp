#include "game/objects.hpp"
#include <bit>
#include <numbers>

namespace lifeline::script {
Bindings Objects::animation_bindings(){
    Bindings result;
    result.emplace(313,Binding{0,0,[this](const Invocation&){object_animations_.clear();object_animations_.shrink_to_fit();return Reply{};}});
    result.emplace(312,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value) || value!=std::nearbyint(value))throw std::runtime_error("Invalid animation channel mapping");
        if(c.arguments[0]<0 || c.arguments[1]<0 || c.arguments[2]<-1)throw std::runtime_error("Invalid animation channel mapping");
        auto& animation=object_animations_.at(std::size_t(c.arguments[0]));
        if(animation.nodes.empty())return Reply{false,0.f};
        if(c.arguments[2]>=0 && c.arguments[2]>=get(float(animation.object)).nodes.size())throw std::runtime_error("Animation target node is outside model");
        for(unsigned i=0;i<animation.channels.size();++i)if(animation.channels[i]==c.arguments[1]){animation.nodes[i]=int(c.arguments[2]);break;}
        return Reply{false,0.f};
    }});
    result.emplace(314,Binding{1,0,[this](const Invocation& c){object_animations_.at(std::size_t(c.arguments[0])).nodes.clear();return Reply{false,0.f};}});
    result.emplace(311,Binding{1,0,[this](const Invocation& c){
        const auto count=c.arguments[0];if(!std::isfinite(count) || count<0 || count>1024 || std::floor(count)!=count)throw std::runtime_error("Invalid object animation slot count");
        object_animations_.clear();object_animations_.resize(unsigned(count));return Reply{false,count};
    }});
    result.emplace(321,Binding{3,0,[this](const Invocation& c){
        auto& animation=object_animations_.at(std::size_t(c.arguments[0]));
        const auto name=c.program.string(std::uint32_t(c.arguments[2]));const auto& b=*resources_.find_raw(name).bytes;
        if(b.size()<16 || le32(b.data())!=0x2020616f || le32(b.data()+4)!=0x10000)throw std::runtime_error("Unsupported object animation: "+name);
        const auto frames=le32(b.data()+8),channels=le32(b.data()+12);const std::size_t offset=16+((std::size_t(channels)+3)&~std::size_t(3))*4;
        // Native 11f8e4..11f918 uses the header's frame/channel counts. Some
        // authored members include archive alignment bytes after those frames.
        if(!frames || !channels || channels>1024 || offset>b.size() || std::uint64_t(frames)*channels*32>b.size()-offset)throw std::runtime_error("Invalid object animation extent");
        ObjectAnimation replacement;replacement.object=int(c.arguments[1]);replacement.mode=0;replacement.frame_count=frames;
        const auto& object=get(c.arguments[1]);
        for(unsigned i=0;i<channels;++i){const auto node=le32(b.data()+16+i*4);if(node>=object.nodes.size())throw std::runtime_error("Object animation node outside model");replacement.channels.push_back(node);replacement.nodes.push_back(int(node));}
        const auto number=[&](std::size_t p){const auto v=std::bit_cast<float>(le32(b.data()+p));if(!std::isfinite(v))throw std::runtime_error("Invalid object animation coordinate");return v;};
        for(std::size_t i=0;i<std::size_t(frames)*channels;++i){const auto p=offset+i*32;
            replacement.frames.push_back({{number(p),number(p+4),number(p+8),1},{number(p+16)*std::numbers::pi_v<float>/180,number(p+20)*std::numbers::pi_v<float>/180,number(p+24)*std::numbers::pi_v<float>/180,0}});
        }
        animation=std::move(replacement);return Reply{false,0.f};
    }});
    for(unsigned opcode:{315u,316u,318u})result.emplace(opcode,Binding{2,0,[this,opcode](const Invocation& c){
        auto& animation=object_animations_.at(std::size_t(c.arguments[0]));if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid object animation parameter");
        if(opcode==315){if(c.arguments[1]<0 || c.arguments[1]>2)throw std::runtime_error("Invalid object animation mode");animation.mode=int(c.arguments[1]);}
        else if(opcode==316){animation.position=c.arguments[1];}
        else animation.speed=c.arguments[1];return Reply{false,0.f};
    }});
    return result;
}
void Objects::advance_object_animations(){
    for(auto& animation:object_animations_){
        if(animation.object<0)continue;
        if(!slots_.at(unsigned(animation.object)))continue;
        // 11fe04..11fe0c skips stopped tracks, including explicit seeks.
        // Setting a position only changes the cursor, not the node pose.
        // Sampling it even once overwrites entry_068's authored red lever
        // pose with the opposite face from its inactive setup animation.
        if(animation.mode<=0)continue;
        const float last=float(animation.frame_count-1);animation.position=std::clamp(animation.position,0.f,last);
        const unsigned first=unsigned(animation.position),next=std::min(first+1,animation.frame_count-1);const auto mix=animation.position-first;
        auto& object=get(float(animation.object));
        for(unsigned i=0;i<animation.nodes.size();++i){
            if(animation.nodes[i]<0)continue;
            const auto& a=animation.frames[first*animation.nodes.size()+i];const auto& b=animation.frames[next*animation.nodes.size()+i];auto& node=object.nodes[animation.nodes[i]];
            node.position=a.position*(1-mix)+b.position*mix;
            // These authored tracks rotate about a single hinge axis. Use the
            // shortest angular interval, including the 0/360 boundary.
            node.rotation={a.rotation.x+TransformMotion::angle(b.rotation.x-a.rotation.x)*mix,a.rotation.y+TransformMotion::angle(b.rotation.y-a.rotation.y)*mix,a.rotation.z+TransformMotion::angle(b.rotation.z-a.rotation.z)*mix,0};
            node.basis_sign={1,1,1,0};
        }
        animation.position+=animation.speed;
        if(animation.speed>0 && animation.position>last){if(animation.mode==2 && last>0)animation.position-=last;else {animation.position=last;animation.mode=0;}}
        else if(animation.speed<=0 && animation.position<=0){animation.position=last;if(animation.mode!=2)animation.mode=0;}
    }
}
}
