#include "script/camera.hpp"
#include "rendering/model.hpp"
#include <cmath>
#include <stdexcept>
#include <bit>

namespace lifeline::script {
Camera::Camera(){current_.eye=eye_;current_.target=target_;current_.up={0,1,0,0};current_.focal_pixels=focal_;}
void Camera::initialize(const SceneCamera& camera){current_=camera;eye_=camera.eye;target_=camera.target;focal_=camera.focal_pixels;}
Bindings Camera::bindings(){
    Bindings result;
    result.emplace(577,Binding{2,0,[this](const Invocation& c){
        for(unsigned axis=0;axis<2;++axis){const float value=std::nearbyint(c.arguments[axis]);if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid display offset");display_offset_[axis]=int(value);}
        return Reply{false,0.f};
    }});
    result.emplace(591,Binding{1,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[0]))throw std::runtime_error("Invalid display offset selector");
        return Reply{false,float(display_offset_[std::nearbyint(c.arguments[0])==0?0:1])};
    }});
    result.emplace(1079,Binding{2,0,[this](const Invocation& c){
        // 113c78 -> 1cabd0: project XYZ through the current camera, without
        // adding screen-center XY. Output Z is GS depth; W is reciprocal depth.
        const auto address=[&](float value){
            const float bytes=value*4;const double rounded=std::trunc(double(bytes+std::copysign(.5f,bytes)));
            if(!c.state||!std::isfinite(rounded)||rounded<0||rounded>2147483647.||std::fmod(rounded,4)!=0)throw std::runtime_error("Invalid projection vector address");
            const auto word=unsigned(rounded)/4;if(!c.state->data_contains(c.program,word,4))throw std::runtime_error("Projection vector outside live data");return word;
        };
        const auto output=address(c.arguments[0]),input=address(c.arguments[1]);std::array<float,3> xyz;
        for(unsigned i=0;i<3;++i){const auto found=c.state->data->find(input+i);xyz[i]=found==c.state->data->end()?std::bit_cast<float>(c.program.word(input+i)):found->second;}
        const auto forward=math::normalize_xyz(current_.target+current_.eye*-1);
        const auto right=math::normalize_xyz(math::cross_xyz(forward,current_.up));const auto up=math::cross_xyz(right,forward);
        const math::Vector4 delta{xyz[0]-current_.eye.x,xyz[1]-current_.eye.y,xyz[2]-current_.eye.z,0};
        const float depth=math::dot_xyz(delta,forward),q=depth==0?std::copysign(std::numeric_limits<float>::max(),depth):1/depth;
        const float near=current_.near_plane,far=current_.far_plane;
        const float z=std::isinf(far)?65536*near*q:32768*((-(near+far)+2*near*far*q)/(far-near))+32768;
        const std::array<float,4> value{math::dot_xyz(delta,right)*current_.focal_pixels*q*(current_.mirror_horizontal?-1.f:1.f),-math::dot_xyz(delta,up)*current_.focal_pixels*q,z,q};
        for(unsigned i=0;i<4;++i)(*c.state->data)[output+i]=value[i];return Reply{false,0.f};
    }});
    result.emplace(586,Binding{1,0,[this](const Invocation& c){
        const float value=std::trunc(c.arguments[0]);if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid shadow mode");
        current_.shadow.mode=int(value);return Reply{false,value};
    }});
    for(unsigned opcode:{579u,583u})result.emplace(opcode,Binding{3,0,[this,opcode](const Invocation& c){
        math::Vector4 direction{c.arguments[0],c.arguments[1],c.arguments[2],0};
        if(opcode==583){if(!actor_transform_)throw std::runtime_error("Shadow light requires actor controller position");direction=actor_transform_().columns[3]+direction*(-1);}
        for(float v:{direction.x,direction.y,direction.z})if(!std::isfinite(v))throw std::runtime_error("Invalid shadow direction");
        current_.shadow.direction=math::normalize_xyz(direction);return Reply{false,0.f};
    }});
    result.emplace(582,Binding{1,0,[this](const Invocation& c){
        const float value=std::trunc(c.arguments[0]);if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid shadow opacity");
        current_.shadow.opacity=value;return Reply{false,value};
    }});
    result.emplace(584,Binding{1,0,[this](const Invocation& c){
        const float value=std::trunc(c.arguments[0]);
        if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>INT32_MAX)throw std::runtime_error("Invalid reflection enable value");
        current_.reflection_enabled=value!=0;return Reply{false,value};
    }});
    result.emplace(585,Binding{2,0,[this](const Invocation& c){
        const auto vector=[&](float pointer){
            const double byte=std::round(double(pointer*4.f));
            if(!std::isfinite(byte)||byte<0||byte>INT32_MAX||std::fmod(byte,4)!=0)throw std::runtime_error("Invalid reflection vector pointer");
            const auto word=unsigned(byte)/4;
            if(c.state?!c.state->data_contains(c.program,word,3):std::uint64_t(word)+3>c.program.size())throw std::runtime_error("Reflection vector outside live script data");
            float values[3];for(unsigned i=0;i<3;++i){
                values[i]=word+i<c.program.size()?std::bit_cast<float>(c.program.word(word+i)):0.f;
                if(c.state){const auto it=c.state->data->find(word+i);if(it!=c.state->data->end())values[i]=it->second;}
            }
            return math::Vector4{values[0],values[1],values[2],0};
        };
        const auto normal=vector(c.arguments[0]),point=vector(c.arguments[1]);
        current_.reflection=reflection_matrix(normal,point);return Reply{false,0.f};
    }});
    result.emplace(568,Binding{1,0,[this](const Invocation& c){
        const float value=std::trunc(c.arguments[0]);if(!std::isfinite(value))throw std::runtime_error("Invalid fog enable value");
        current_.fog_enabled=value!=0;return Reply{false,value};
    }});
    result.emplace(569,Binding{3,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value)||value<0||value>=256)throw std::runtime_error("Fog color outside byte range");
        current_.fog_color={std::trunc(c.arguments[0]),std::trunc(c.arguments[1]),std::trunc(c.arguments[2]),0};return Reply{false,0.f};
    }});
    result.emplace(570,Binding{2,0,[this](const Invocation& c){
        const float near=c.arguments[0],far=c.arguments[1];
        if(!std::isfinite(near)||!std::isfinite(far)||near==far)throw std::runtime_error("Invalid fog distance range");
        current_.fog_near=near;current_.fog_far=far;return Reply{false,0.f};
    }});
    result.emplace(549,Binding{1,0,[this](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value)||std::trunc(value)<0||std::trunc(value)>=32)throw std::runtime_error("Invalid camera object slot");
        tracked_object_=unsigned(std::trunc(value));return Reply{false,float(tracked_object_)};
    }});
    result.emplace(580,Binding{0,0,[this](const Invocation&){
        current_.eye=eye_;current_.target=target_;current_.up=math::normalize_xyz(up_);current_.focal_pixels=focal_;return Reply{false,0.f};
    }});
    result.emplace(574,Binding{0,1,[this](const Invocation& c){
        const float value=std::bit_cast<float>(c.operands[0]);if(!std::isfinite(value))throw std::runtime_error("Invalid camera shake amplitude");
        shaking_=true;shake_amplitude_=value;return Reply{};
    }});
    result.emplace(575,Binding{0,0,[this](const Invocation&){shaking_=false;shake_amplitude_=0;return Reply{};}});
    result.emplace(578,Binding{2,0,[this](const Invocation& c){
        const float near=c.arguments[0],far=c.arguments[1];
        if(!std::isfinite(near)||!std::isfinite(far)||near<=0||far<=near)throw std::runtime_error("Invalid camera clipping range");
        current_.near_plane=near;current_.far_plane=far;return Reply{false,0.f};
    }});
    result.emplace(544,Binding{0,1,[this](const Invocation& c){
        if(!resources_)throw std::runtime_error("Camera track has no installed resource service");
        const auto& file=resources_->find_raw(c.program.string(c.operands[0]));
        track_.emplace(*file.bytes);track_frame_=0;track_step_=track_->step();return Reply{};
    }});
    result.emplace(571,Binding{0,0,[this](const Invocation&){return Reply{(mode_==13 || mode_==14) && track_ && track_frame_<float(track_->size()-1),{}};}});
    result.emplace(572,Binding{0,0,[this](const Invocation&){track_frame_=0;return Reply{};}});
    result.emplace(573,Binding{0,1,[this](const Invocation& c){
        const float step=std::bit_cast<float>(c.operands[0]);if(!std::isfinite(step) || step<0)throw std::runtime_error("Invalid camera track step");track_step_=step;return Reply{};
    }});
    result.emplace(562,Binding{3,0,[this](const Invocation& c){
        math::Vector4 up{c.arguments[0],c.arguments[1],c.arguments[2],0};
        const float length=math::dot_xyz(up,up);if(!std::isfinite(length) || length<=0)throw std::runtime_error("Invalid camera up vector");
        up_=math::normalize_xyz(up);automatic_up_=false;return Reply{false,0.f};
    }});
    result.emplace(563,Binding{0,0,[this](const Invocation&){automatic_up_=true;return Reply{};}});
    result.emplace(581,Binding{4,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid camera fill light");
        current_.fill_light={c.arguments[0],c.arguments[1],c.arguments[2],c.arguments[3]};return Reply{false,0.f};
    }});
    result.emplace(545,Binding{1,0,[this](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value) || value<0 || value!=std::floor(value) || value>14)throw std::runtime_error("Camera mode not recovered: "+std::to_string(value));
        mode_=unsigned(value);if(mode_==13){track_origin_.reset();if(actor_transform_)track_origin_=actor_transform_();}if(!smooth_)cut_=true;return Reply{false,value};
    }});
    result.emplace(546,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::logic_error("Camera mode query requires worker locals");c.state->integers[c.operands[0]]=mode_;return Reply{};
    }});
    for(const auto opcode:{555u,557u})result.emplace(opcode,Binding{3,0,[this,opcode](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid local camera coordinate");
        (opcode==555?local_eye_:local_target_)={c.arguments[0],c.arguments[1],c.arguments[2],1};if(!smooth_)cut_=true;return Reply{false,0.f};
    }});
    for(const auto opcode:{552u,554u,556u,558u})result.emplace(opcode,Binding{0,3,[this,opcode](const Invocation& c){
        if(!c.state)throw std::logic_error("Camera coordinate query requires worker locals");
        const auto value=opcode==552?current_.eye:opcode==554?current_.target:opcode==556?local_eye_:local_target_;
        c.state->floats[c.operands[0]]=value.x;c.state->floats[c.operands[1]]=value.y;c.state->floats[c.operands[2]]=value.z;return Reply{};
    }});
    result.emplace(547,Binding{0,1,[this](const Invocation& c){smooth_=c.operands[0]!=0;return Reply{};}});
    result.emplace(548,Binding{2,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[0]) || !std::isfinite(c.arguments[1]) || c.arguments[0]<0 || c.arguments[1]<0)throw std::runtime_error("Invalid camera tracking limits");
        limit_=c.arguments[0];rate_=c.arguments[1];return Reply{false,0.f};
    }});
    for(const auto opcode:{550u,553u})result.emplace(opcode,Binding{3,0,[this,opcode](const Invocation& c){
        for(const auto value:c.arguments)if(!std::isfinite(value))throw std::runtime_error("Invalid camera coordinate");
        (opcode==550?eye_:target_)={c.arguments[0],c.arguments[1],c.arguments[2],0};if(!smooth_)cut_=true;return Reply{false,0.f};
    }});
    result.emplace(559,Binding{1,0,[this](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value) || value<=0)throw std::runtime_error("Invalid camera focal length");
        focal_=value;return Reply{false,value};
    }});
    result.emplace(560,Binding{0,0,[this](const Invocation&){return Reply{false,focal_};}});
    return result;
}
void Camera::advance(std::span<const math::Matrix4> actor_pose,std::optional<math::Matrix4> actor_world){
    // 131c98 / 131e90 follow bone 11 / 39. Mode 6 retains script coordinates.
    if(mode_==13 || mode_==14){
        if(track_){
            track_frame_=std::min(track_frame_,float(track_->size()-1));auto sample=track_->sample_authored(track_frame_);
            if(mode_==13){
                if(!track_origin_){if(!actor_world)throw std::runtime_error("Relative camera track requires actor transform");track_origin_=*actor_world;}
                sample.eye=math::transform(*track_origin_,sample.eye);sample.target=math::transform(*track_origin_,sample.target);sample.up=math::transform(*track_origin_,sample.up);
            }
            eye_=sample.eye;target_=sample.target;up_=sample.up;focal_=sample.focal_pixels;
            smooth_=false;track_frame_+=track_step_;
        }
    }
    else if(mode_==9 || mode_==10){
        if(!object_transform_)throw std::runtime_error("Object camera requires the scene object service");
        if(const auto world=object_transform_(tracked_object_,mode_==9)){
            if(mode_==9)target_=math::transform(*world,local_target_);
            else {
                if(!actor_world)throw std::runtime_error("Two-subject camera requires Rio's world transform");
                target_=(math::transform(*actor_world,{0,15,0,1})+world->columns[3])*.5f;target_.w=1;
            }
        }
    }
    else if(mode_==0 || mode_==5){const unsigned bone=mode_==0?11:39;if(actor_pose.size()<=bone)throw std::runtime_error("Camera requires actor pose");target_=actor_pose[bone].columns[3];}
    else if(mode_!=6){
        if(!actor_world)throw std::runtime_error("Actor-relative camera requires the model world transform");
        const auto set=[&](math::Vector4 eye,math::Vector4 target){eye_=math::transform(*actor_world,eye);target_=math::transform(*actor_world,target);};
        switch(mode_){
        case 1:set({0,14,-20,1},{0,13,0,1});break;
        case 2:set({0,8,-20,1},{0,13,0,1});break;
        case 3:set({0,20,20,1},{0,13,0,1});break;
        case 4:set({0,15,6,1},{0,15.5f,0,1});break;
        case 7:target_=math::transform(*actor_world,local_target_);break;
        case 8:set({0,15,1,1},{0,13,20,1});break;
        case 11:set(local_eye_,local_target_);break;
        case 12:eye_=math::transform(*actor_world,local_eye_);break;
        default:throw std::logic_error("Unimplemented camera mode reached update");
        }
    }
    const auto approach=[&](math::Vector4 from,math::Vector4 to){
        auto delta=(to+from*(-1))*rate_;const float length=std::sqrt(math::dot_xyz(delta,delta));
        if(length>limit_)delta=delta*(limit_/length);return from+delta;
    };
    if(smooth_ && !cut_){current_.eye=approach(current_.eye,eye_);current_.target=approach(current_.target,target_);}
    else {current_.eye=eye_;current_.target=target_;}
    // 130b7c: perturb the displayed target on alternate updates. The original
    // performs its decay in double precision before storing the next float.
    if(shaking_ && (update_phase_&1)){current_.target.y+=shake_amplitude_;shake_amplitude_=float(double(shake_amplitude_)*-.8);}
    ++update_phase_;
    current_.focal_pixels=focal_;cut_=false;
    if(mode_<13 && automatic_up_ && actor_world)up_=math::transform(*actor_world,{0,1,0,0});
    current_.up=math::normalize_xyz(up_);
}
}
