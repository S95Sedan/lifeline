#include "game/objects.hpp"
#include <bit>
#include <cmath>

namespace lifeline::script {
namespace {
unsigned integer(float value){
    if(!std::isfinite(value)||value<0||value>=2147483648.f)throw std::runtime_error("Invalid model animation identifier");
    // Original CVT.W.S runs with the game's truncating FCR31 mode. Fractional
    // script expressions such as Sun Suite's 0.6 animation mode therefore mean
    // one-shot (0), not loop (1).
    return unsigned(std::trunc(value));
}
math::Vector4 step(AnimationCursor& cursor,const SkeletalClip& clip){
    const auto sample=[&](float frame){const int i=int(frame);return clip.frame(i).root_motion*(1-(frame-i))+clip.frame(i+1).root_motion*(frame-i);};
    const float before=cursor.position();const auto start=sample(before);cursor.advance();auto delta=sample(cursor.position())+start*(-1);
    if(cursor.mode()==1){
        if(cursor.speed()>0 && cursor.position()<before)delta=delta+clip.frames.back().root_motion+clip.frames.front().root_motion*(-1);
        if(cursor.speed()<0 && cursor.position()>before)delta=delta+clip.frames.front().root_motion+clip.frames.back().root_motion*(-1);
    }
    delta.y=0;delta.w=0;return delta;
}
}
void Objects::Instance::load_animations(AnimationBank bank){
    if(bank.clips.empty())throw std::runtime_error("Empty model animation bank");
    // 1c5fc8 selects clip zero in both inactive controller slots.
    if(skeleton && skin){skeletal_pose=sample_skeleton(*skeleton,bank.clips[0],0.f,RootMotion::height_only);pose_skin_surface(model,*skin,*skeleton,skeletal_pose);}
    animations=std::make_shared<AnimationBank>(std::move(bank));secondary_animations=animations;displacement={};
    playing_clip.reset();prior_clip.reset();cursor.reset();previous_cursor.reset();clip=previous_clip=0;blend=1;blend_step=1;animation_paused=false;displacement_suppressed=false;hold_completed_animation=false;pending_animation.reset();
}
int Objects::Instance::select_animation(unsigned id,unsigned mode,float speed,float transition,std::optional<float> start,bool force){
    const auto bank=id>=1000?secondary_animations:animations;const unsigned index=id>=1000?id-1000:id;
    if(!bank || index>=bank->clips.size())throw std::runtime_error("Model animation outside loaded bank");
    if(!std::isfinite(transition))throw std::runtime_error("Invalid model animation blend");
    if(hold_completed_animation&&!force&&cursor&&!cursor->playing()&&clip==id){pending_animation=PendingAnimation{id,mode,speed,transition,start,force};return 0;}
    pending_animation.reset();
    // 1182c0..11839c: the fast path is restricted to an already looping clip.
    if(!force && cursor && cursor->playing() && cursor->mode()==1 && clip==id && cursor->speed()==speed && (!start || cursor->position()==*start)){
        if(mode==cursor->mode())return 0;cursor->mode(mode);return 2;
    }
    AnimationCursor replacement(bank->clips[index],mode,speed,start.value_or(-1.f));
    displacement.select();
    prior_clip=playing_clip;playing_clip=std::shared_ptr<const SkeletalClip>(bank,&bank->clips[index]);
    previous_cursor=cursor;previous_clip=clip;cursor=replacement;clip=id;
    blend=transition>=0 && transition<1 && previous_cursor?0.f:1.f;blend_step=transition;
    if(blend==1){previous_cursor.reset();prior_clip.reset();}return 1;
}
void Objects::Instance::advance_skeleton(){
    if(!animations || !cursor || animation_paused){pose_overrides.clear();return;}
    const auto current=step(*cursor,*playing_clip);
    std::optional<math::Vector4> prior;if(previous_cursor)prior=step(*previous_cursor,*prior_clip);
    const auto delta=displacement.advance(current,prior,blend,displacement_suppressed);
    const auto rotation=transform.matrix();const auto world_delta=math::transform(rotation,delta);
    if(!displacement_suppressed)transform.position=transform.position+world_delta;transform.position.w=1;
    if(previous_cursor){blend=std::min(1.f,blend+blend_step);if(blend==1){previous_cursor.reset();prior_clip.reset();}}
    if(skeleton && skin){
        const auto policy=root_pose_disabled?RootMotion::disabled:RootMotion::height_only;
        skeletal_pose=previous_cursor?blend_skeleton(*skeleton,*prior_clip,previous_cursor->position(),*playing_clip,cursor->position(),blend,policy):sample_skeleton(*skeleton,*playing_clip,cursor->position(),policy);
        // 1dc360 preserves explicitly supplied final bone matrices for one update.
        for(const auto& [bone,matrix]:pose_overrides)skeletal_pose.at(bone)=matrix;
        pose_overrides.clear();
        pose_skin_surface(model,*skin,*skeleton,skeletal_pose);
    }
}
void Objects::hold_completed_object_animation(unsigned object,bool hold){
    auto* selected=slots_.at(object).get();if(!selected)throw std::runtime_error("Held model animation object is not installed");
    auto& instance=*selected;
    if(instance.hold_completed_animation==hold)return;
    instance.hold_completed_animation=hold;
    if(!hold&&instance.pending_animation){
        const auto pending=*instance.pending_animation;instance.pending_animation.reset();
        if(instance.select_animation(pending.id,pending.mode,pending.speed,pending.transition,pending.start,pending.force)==1)play_motion_sound(object,pending.id);
    }
}
Bindings Objects::model_animation_bindings(){
    Bindings result=motion_sound_bindings();
    result.emplace(343,Binding{1,0,[this](const Invocation& c){
        const auto& object=get(std::nearbyint(c.arguments[0]));
        if(!object.animations)throw std::runtime_error("Model has no animation controller");
        const auto height=[](const SkeletalClip& clip,float frame){return clip.frame(int(std::nearbyint(frame))).root_motion.y+clip.root_origin.y;};
        const float current=height(object.playing_clip?*object.playing_clip:object.animations->clips.at(0),object.cursor?object.cursor->position():0.f);
        return Reply{false,object.previous_cursor?height(*object.prior_clip,object.previous_cursor->position())*(1-object.blend)+current*object.blend:current};
    }});
    result.emplace(328,Binding{1,0,[this](const Invocation& c){
        auto& object=get(std::trunc(c.arguments[0]));
        if(!object.animations)throw std::runtime_error("Model has no animation controller");
        return Reply{false,object.displacement.horizontal(object.blend,object.displacement_suppressed)};
    }});
    result.emplace(243,Binding{1,0,[this](const Invocation& c){
        const auto& object=get(float(std::nearbyint(c.arguments[0])));
        if(!object.animations)throw std::runtime_error("Model has no animation controller");
        return Reply{object.cursor && object.cursor->playing(),0.f};
    }});
    result.emplace(219,Binding{6,0,[this](const Invocation& c){
        const auto object=slot(c.arguments[0]),animation=integer(c.arguments[1]);auto& model=get(float(object));
        const auto suppressed=std::nearbyint(c.arguments[3]);if(!std::isfinite(suppressed))throw std::runtime_error("Invalid model displacement policy");
        if(model.select_animation(animation,integer(c.arguments[2]),c.arguments[4],c.arguments[5])==1)play_motion_sound(object,animation);
        model.displacement_suppressed=suppressed!=0;return Reply{false,0.f};
    }});
    for(unsigned opcode:{174u,176u})result.emplace(opcode,Binding{opcode==174?0u:3u,opcode==174?3u:0u,[this,opcode](const Invocation& c){
        const auto id=slot(opcode==174?float(c.operands[0]):float(integer(c.arguments[0])));
        const auto model=opcode==174?c.operands[1]:integer(c.arguments[1]);
        const auto animation=opcode==174?c.operands[2]:integer(c.arguments[2]);
        const auto name=c.program.string(model);
        auto replacement=std::make_unique<Instance>(resources_.find_raw(name),name);
        replacement->load_animations(decode_animation_bank(*resources_.find_raw(c.program.string(animation)).bytes));
        lights_.replace(replacement->model.lights);
        slots_[id]=std::move(replacement);return opcode==174?Reply{}:Reply{false,float(id)};
    }});
    result.emplace(177,Binding{0,2,[this](const Invocation& c){
        auto& object=get(float(c.operands[0]));if(!object.skeleton)throw std::runtime_error("Animation bank replacement requires a skeletal model");
        auto bank=decode_animation_bank(*resources_.find_raw(c.program.string(c.operands[1])).bytes);
        if(bank.clips.empty())throw std::runtime_error("Empty replacement animation bank");
        if(!object.animations)object.load_animations(std::move(bank));else object.animations=std::make_shared<AnimationBank>(std::move(bank));return Reply{};
    }});
    for(unsigned opcode:{220u,222u})result.emplace(opcode,Binding{opcode==220?4u:5u,0,[this,opcode](const Invocation& c){
        const auto object=slot(c.arguments[0]),animation=integer(c.arguments[1]);
        const auto result=get(float(object)).select_animation(animation,integer(c.arguments[2]),c.arguments[3],opcode==222?c.arguments[4]:-1.f);
        if(result==1)play_motion_sound(object,animation);return Reply{false,float(result)};
    }});
    result.emplace(221,Binding{0,5,[this](const Invocation& c){
        const auto selected=get(float(c.operands[0])).select_animation(c.operands[1],c.operands[2],std::bit_cast<float>(c.operands[3]),std::bit_cast<float>(c.operands[4]));if(selected==1)play_motion_sound(c.operands[0],c.operands[1]);return Reply{};
    }});
    result.emplace(230,Binding{0,2,[this](const Invocation& c){
        auto& object=get(float(c.operands[0]));if(!object.cursor)throw std::runtime_error("Model animation has not started");object.cursor->mode(c.operands[1]);return Reply{};
    }});
    for(unsigned opcode:{223u,225u})result.emplace(opcode,Binding{opcode==223?5u:6u,0,[this,opcode](const Invocation& c){
        return Reply{false,float(get(c.arguments[0]).select_animation(integer(c.arguments[1]),integer(c.arguments[2]),c.arguments[3],opcode==225?c.arguments[5]:-1.f,c.arguments[4]))};
    }});
    result.emplace(224,Binding{0,6,[this](const Invocation& c){
        get(float(c.operands[0])).select_animation(c.operands[1],c.operands[2],std::bit_cast<float>(c.operands[3]),std::bit_cast<float>(c.operands[5]),std::bit_cast<float>(c.operands[4]));return Reply{};
    }});
    for(unsigned opcode:{226u,227u})result.emplace(opcode,Binding{0,opcode==226?4u:5u,[this,opcode](const Invocation& c){
        auto& object=get(float(c.operands[0]));object.select_animation(c.operands[1],c.operands[2],std::bit_cast<float>(c.operands[3]),opcode==227?std::bit_cast<float>(c.operands[4]):-1.f,{},true);
        const auto& selected=*object.playing_clip;const float frame=object.cursor->position();const int i=int(frame);
        const auto origin=selected.frame(i).root_motion*(1-(frame-i))+selected.frame(i+1).root_motion*(frame-i)+selected.root_origin;
        object.transform.position={origin.x,0,origin.z,1};return Reply{};
    }});
    for(unsigned opcode:{228u,229u})result.emplace(opcode,Binding{2,0,[this,opcode](const Invocation& c){
        auto& object=get(c.arguments[0]);const auto value=c.arguments[1];if(!std::isfinite(value))throw std::runtime_error("Invalid model root policy");
        const bool enabled=std::nearbyint(value)!=0;if(opcode==228)object.displacement_suppressed=enabled;else object.root_pose_disabled=enabled;return Reply{false,0.f};
    }});
    result.emplace(231,Binding{2,0,[this](const Invocation& c){
        auto& object=get(c.arguments[0]);if(!object.cursor)throw std::runtime_error("Model animation has not started");object.cursor->speed(c.arguments[1]);return Reply{};
    }});
    for(unsigned opcode:{237u,238u})result.emplace(opcode,Binding{0,1,[this,opcode](const Invocation& c){
        auto& object=get(float(c.operands[0]));if(!object.animations)throw std::runtime_error("Model has no animation controller");object.animation_paused=opcode==237;return Reply{};
    }});
    for(unsigned opcode:{233u,234u})result.emplace(opcode,Binding{0,2,[this,opcode](const Invocation& c){
        const auto& object=get(float(c.operands[0]));if(!object.animations || !c.state)throw std::runtime_error("Model animation query requires controller and locals");
        if(opcode==233)c.state->integers[c.operands[1]]=int(object.clip);else c.state->floats[c.operands[1]]=object.cursor?object.cursor->position():0.f;return Reply{};
    }});
    for(unsigned opcode:{235u,236u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const auto& object=get(c.arguments[0]);if(!object.animations)throw std::runtime_error("Model has no animation controller");
        return Reply{false,opcode==235?float(object.clip):object.cursor?object.cursor->position():0.f};
    }});
    return result;
}
}
