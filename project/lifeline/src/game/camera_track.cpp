#include "game/camera_track.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>

namespace lifeline {
CameraTrack::CameraTrack(std::span<const unsigned char> bytes) {
    const auto u32=[&](std::size_t p){
        if(p>bytes.size() || bytes.size()-p<4)throw std::runtime_error("Truncated camera track");
        return std::uint32_t(bytes[p]) | std::uint32_t(bytes[p+1])<<8 |
               std::uint32_t(bytes[p+2])<<16 | std::uint32_t(bytes[p+3])<<24;
    };
    const auto number=[&](std::size_t p){const auto f=std::bit_cast<float>(u32(p));
        if(!std::isfinite(f))throw std::runtime_error("Nonfinite camera sample");return f;};
    if(u32(0)!=0x006d6163 || u32(4)!=0x20010802 || u32(12)>1)
        throw std::runtime_error("Unsupported camera track header");
    const auto count=u32(8);
    // ELF 12fa98..12fad4: flagged tracks advance half a sample per tick.
    step_=u32(12)? .5f:1.f;
    const auto payload=16+std::size_t(count)*64;
    // Event archives contain files rounded to 64 or 128 bytes. Those trailing
    // allocation bytes can contain stale data and are not camera samples.
    if(!count || count>1000000 || (bytes.size()!=payload && bytes.size()!=((payload+63)&~std::size_t(63)) && bytes.size()!=((payload+127)&~std::size_t(127))))
        throw std::runtime_error("Invalid camera sample count");
    frames_.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto p=16+i*64;
        // Match the native room's reflected Y coordinate convention.
        const auto vector=[&](std::size_t at,float w){return math::Vector4{number(at),-number(at+4),number(at+8),w};};
        SceneCamera c;c.eye=vector(p,1);c.target=vector(p+16,1);c.up=vector(p+32,0);
        c.focal_pixels=number(p+48);c.mirror_horizontal=true;
        // The original track loader copies these vectors without requiring a
        // view basis. s006/Cam6-01 frame 421 is stored in the file
        // with coincident eye/target; do not replace or reject the whole track.
        if(c.focal_pixels<=0)throw std::runtime_error("Invalid camera focal length");
        frames_.push_back(c);
        cut_.push_back(number(p+52)!=0);
    }
}
SceneCamera CameraTrack::sample(double frame,bool loop) const {
    if(!std::isfinite(frame))throw std::runtime_error("Invalid camera frame position");
    if(loop){frame=std::fmod(frame,double(frames_.size()));if(frame<0)frame+=double(frames_.size());}
    else frame=std::clamp(frame,0.,double(frames_.size()-1));
    const auto a=std::size_t(frame),b=loop?(a+1)%frames_.size():std::min(a+1,frames_.size()-1);
    const auto t=float(frame-double(a));auto result=frames_[a];const auto& end=frames_[b];
    result.eye=result.eye*(1-t)+end.eye*t;result.target=result.target*(1-t)+end.target*t;
    result.up=math::normalize_xyz(result.up*(1-t)+end.up*t);
    result.focal_pixels=result.focal_pixels*(1-t)+end.focal_pixels*t;
    return result;
}
SceneCamera CameraTrack::sample_authored(float frame) const {
    if(!std::isfinite(frame) || frame<0)throw std::runtime_error("Invalid authored camera frame");
    frame=std::min(frame,float(frames_.size()-1));
    const unsigned first=unsigned(frame),next=std::min(first+1,unsigned(frames_.size()-1));
    const float mix=cut_[first]?0:frame-float(first);
    auto result=frames_[first];const auto& end=frames_[next];
    result.eye=result.eye*(1-mix)+end.eye*mix;result.target=result.target*(1-mix)+end.target*mix;
    result.up=result.up*(1-mix)+end.up*mix;
    result.focal_pixels=result.focal_pixels*(1-mix)+end.focal_pixels*mix;
    // The script service owns authored coordinates. Reflection stays in renderer.
    result.eye.y=-result.eye.y;result.target.y=-result.target.y;result.up.y=-result.up.y;result.mirror_horizontal=false;
    return result;
}
}
