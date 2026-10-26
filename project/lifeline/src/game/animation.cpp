#include "game/animation.hpp"
#include <bit>
#include <numbers>

namespace lifeline {
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct Reader {
    std::span<const unsigned char> b;
    void check(std::size_t p,std::size_t n) const {require(p<=b.size() && n<=b.size()-p,"Animation extent outside asset");}
    unsigned u32(std::size_t p) const {check(p,4);return le32(b.data()+p);}
    unsigned u16(std::size_t p) const {check(p,2);return le16(b.data()+p);}
    float number(std::size_t p) const {auto f=std::bit_cast<float>(u32(p));require(std::isfinite(f) && std::abs(f)<1e7f,"Invalid animation coordinate");return f;}
    math::Vector4 vector(std::size_t p) const {return {number(p),number(p+4),number(p+8),1};}
    bool magic(std::size_t p,std::string_view text) const {check(p,text.size());return std::equal(text.begin(),text.end(),b.begin()+p);}
};
// Original matrix builder 0x1d0570: Rz * Ry * Rx, signed 16-bit turns.
math::Matrix4 rotation(BoneAngles a) {
    constexpr float unit=2*std::numbers::pi_v<float>/65536;
    const float sx=std::sin(a.x*unit),cx=std::cos(a.x*unit),sy=std::sin(a.y*unit),cy=std::cos(a.y*unit),sz=std::sin(a.z*unit),cz=std::cos(a.z*unit);
    return {{math::Vector4{cy*cz,cy*sz,-sy,0},math::Vector4{sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy,0},
             math::Vector4{cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy,0},math::Vector4{0,0,0,1}}};
}
}
const SkeletalFrame& SkeletalClip::frame(int index) const {
    if(!unsupported_reason.empty()) throw std::runtime_error(unsupported_reason);
    require(!frames.empty(),"Empty skeletal clip");return frames[std::size_t(std::clamp(index,0,int(frames.size()-1)))];
}
AnimationCursor::AnimationCursor(const SkeletalClip& clip,unsigned mode,float speed,float start):speed_(speed),mode_(mode) {
    clip.frame(0);require(mode<=1 && std::isfinite(speed) && std::isfinite(start),"Unsupported animation cursor settings");
    last_=float(clip.frames.size()-1);position_=start>=0?start:speed<0?last_:0;
}
void AnimationCursor::advance() {
    if(!playing_) return;
    position_+=speed_;
    if(mode_==0) {
        if(position_>last_) {position_=last_;playing_=false;}
        else if(position_<0) {position_=0;playing_=false;}
    } else {
        // Preserve the original single subtraction/addition and inclusive
        // forward endpoint, including fractional overshoot. No extra hold frame.
        if(position_>=last_) position_-=last_;
        else if(position_<0) position_+=last_;
    }
}
const SkeletalClip& resolve_animation_clip(const AnimationBank& shared,const AnimationBank& scene,unsigned animation_id) {
    const auto& bank=animation_id>=1000?scene:shared;
    const auto index=animation_id>=1000?animation_id-1000:animation_id;
    require(index<bank.clips.size(),"Animation identifier outside selected bank");
    const auto& clip=bank.clips[index];clip.frame(0);return clip;
}
AnimationBank decode_animation_bank(std::span<const unsigned char> bytes) {
    Reader r{bytes};r.check(0,16);
    require(r.u32(0)==0x006d6e61 && r.u32(4)==0x00040000,"Unsupported animation bank version");
    const unsigned count=r.u32(8);require(count>0 && count<=4096,"Invalid animation clip count");r.check(16,count*4);
    AnimationBank bank;std::size_t total_rotations=0;
    for(unsigned i=0;i<count;++i) {
        const std::size_t begin=r.u32(16+i*4),end=i+1<count?r.u32(20+i*4):bytes.size();
        require(begin>=16+count*4 && begin%16==0 && end>begin && end<=bytes.size(),"Invalid animation clip range");
        Reader clip{bytes.subspan(begin,end-begin)};clip.check(0,48);
        if(clip.magic(0,"ANIMCOM0222")) {
            SkeletalClip unavailable;unavailable.source_offset=unsigned(begin);unavailable.unsupported_reason="ANIMCOM0222 clip is not supported by the recovered frame decoder";
            bank.clips.push_back(std::move(unavailable));continue;
        }
        require(clip.magic(0,"ANIMCOM1110"),"Unsupported animation clip version");
        const unsigned channels=clip.u16(16),frames=clip.u16(18),slots=clip.u16(20),blocks=(frames+15)/16;
        require(channels>0 && channels<=112 && frames>0 && slots>=blocks && slots<=blocks+3,"Invalid animation clip counts");
        clip.check(48,slots*4);total_rotations+=std::size_t(channels)*frames;
        require(total_rotations<=16000000,"Animation bank exceeds decoded frame budget");
        SkeletalClip result;result.source_offset=unsigned(begin);result.channel_count=channels;result.root_origin=clip.vector(32);
        result.frames.reserve(frames);std::size_t previous_end=48+slots*4;
        for(unsigned j=0;j<blocks;++j) {
            const std::size_t p=clip.u32(48+j*4);require(p>=previous_end && p%16==0,"Overlapping animation blocks");clip.check(p,16);
            const unsigned stride=clip.u16(p+14),used=std::min(16u,frames-j*16);
            std::vector<unsigned> active;
            for(unsigned bit=0;bit<112;++bit) if(clip.b[p+bit/8]&(1u<<(bit%8))) {
                require(bit<channels,"Animation mask exceeds channel count");active.push_back(bit);
            }
            require(!active.empty() && active[0]==0 && stride==((12+active.size()*6+3)&~std::size_t(3)),"Animation stride disagrees with channel mask");
            clip.check(p+16,std::size_t(stride)*used);previous_end=p+16+std::size_t(stride)*used;
            for(unsigned k=0;k<used;++k) {
                auto frame=p+16+std::size_t(k)*stride;
                SkeletalFrame sample;sample.root_motion=clip.vector(frame);sample.rotations.resize(channels);frame+=12;
                for(auto channel:active) {
                    sample.rotations[channel]={static_cast<std::int16_t>(clip.u16(frame)),static_cast<std::int16_t>(clip.u16(frame+2)),static_cast<std::int16_t>(clip.u16(frame+4))};frame+=6;
                }
                result.frames.push_back(std::move(sample));
            }
        }
        bank.clips.push_back(std::move(result));
    }
    return bank;
}
Skeleton decode_skeleton(std::span<const unsigned char> bytes) {
    Reader r{bytes};r.check(0,64);require(r.magic(0,"SKICOM0830") || r.magic(0,"SKICOM0828") || r.magic(0,"SKICOM0829"),"Unsupported skeleton version");
    // Bone records always start at +64. The independent primary geometry
    // pointer at +16 may be null in secondary-stream-only models.
    const unsigned count=r.u16(32);require(count>0 && count<=112,"Unsupported skeleton record table");r.check(64,count*256);
    Skeleton result;
    for(unsigned i=0;i<count;++i) {
        const auto p=64+i*256;const auto parent=std::bit_cast<std::int32_t>(r.u32(p));
        require((i==0 && parent==-1) || (i>0 && (parent==-2 || (parent>=0 && unsigned(parent)<i))),"Unsupported or cyclic skeleton hierarchy");
        // 0x1dc908..0x1dc984 takes clip rotations directly (identity for omitted
        // channels), rather than adding the setup angles at bone +0x10.
        r.number(p+16);r.number(p+20);r.number(p+24);require(r.number(p+28)==1,"Unsupported skeleton setup rotation");
        // 1dea60 initializes every runtime aiming axis to +Z. The vector
        // stored in the disc record is overwritten by that loader.
        SkeletonBone bone;bone.parent=parent;bone.translation=r.vector(p+32);
        for(unsigned column=0;column<4;++column) {
            bone.inverse_bind.columns[column]=r.vector(p+64+column*16);
            bone.inverse_bind.columns[column].w=column==3?1.f:0.f;
        }
        const auto name_end=std::find(bytes.begin()+p+128,bytes.begin()+p+160,0);
        bone.name=std::string(bytes.begin()+p+128,name_end);result.bones.push_back(std::move(bone));
    }
    // FANICOM1010 is an aligned embedded model section. The original loader
    // finds it through the relocated model descriptor (1dc248..1dc2a0).
    const unsigned facial_count=r.u16(36),facial_first=r.u16(40);
    for(std::size_t p=64+count*256;p+32<=bytes.size();p+=16){
        if(!r.magic(p,"FANICOM1010"))continue;
        require(!result.facial,"Duplicate facial pose section");
        const unsigned channels=r.u32(p+16),poses=r.u32(p+20),stride=r.u32(p+24);
        require(channels==facial_count && channels>0 && facial_first<count && channels<=count-facial_first,"Facial pose range differs from skeleton");
        require(poses>0 && poses<=1024 && stride==((channels*12+15)&~15u),"Unsupported facial pose table");
        r.check(p+32,std::size_t(poses)*stride);
        FacialPoses face;face.first_bone=facial_first;face.poses.resize(poses);
        for(unsigned pose=0;pose<poses;++pose){
            auto& angles=face.poses[pose];angles.reserve(channels);
            for(unsigned bone=0;bone<channels;++bone){
                const auto q=p+32+std::size_t(pose)*stride+bone*12;
                angles.push_back({std::bit_cast<std::int16_t>(std::uint16_t(r.u16(q+6))),std::bit_cast<std::int16_t>(std::uint16_t(r.u16(q+8))),std::bit_cast<std::int16_t>(std::uint16_t(r.u16(q+10)))});
            }
        }
        result.facial=std::move(face);p+=std::size_t(poses)*stride+16;
    }
    return result;
}
void AnimationCursor::speed(float value){require(std::isfinite(value),"Invalid animation speed");speed_=value;}
void AnimationCursor::mode(unsigned value){require(value<=1,"Unsupported animation loop mode");mode_=value;}
namespace {
math::Matrix4 blend_basis(const math::Matrix4& a,const math::Matrix4& b,float t) {
    // 0x1d06a8 copies endpoints. In between: blend X/Y, normalize,
    // reconstruct Z=X cross Y, normalize Z, reconstruct Y=Z cross X.
    if(t<=0) return a;
    if(t>=1) return b;
    const auto x=math::normalize_xyz(a.columns[0]*(1-t)+b.columns[0]*t);
    const auto y=math::normalize_xyz(a.columns[1]*(1-t)+b.columns[1]*t);
    const auto z=math::normalize_xyz(math::cross_xyz(x,y));
    return {{x,math::cross_xyz(z,x),z,{0,0,0,1}}};
}
}
void apply_facial_expression(const Skeleton& skeleton,std::span<math::Matrix4> pose,const FacialExpression& expression){
    if(!skeleton.facial)return;
    require(pose.size()==skeleton.bones.size(),"Facial pose lacks body bones");
    const auto& face=*skeleton.facial;
    for(auto index:expression.poses)require(index<face.poses.size(),"Facial expression outside bank");
    for(auto weight:{expression.first_weight,expression.second_weight,expression.mix_weight})require(std::isfinite(weight),"Non-finite facial blend weight");
    const float first=std::clamp(expression.first_weight,0.f,1.f),second=std::clamp(expression.second_weight,0.f,1.f),mix=std::clamp(expression.mix_weight,0.f,1.f);
    for(unsigned i=0;i<face.poses.front().size();++i){
        const auto bone=face.first_bone+i;
        // 1df0c0 skips both eye joints when either authored eye flag is set.
        if(expression.preserve_eyes && (bone==62 || bone==63))continue;
        const auto a=blend_basis(rotation(face.poses[expression.poses[0]][i]),rotation(face.poses[expression.poses[1]][i]),first);
        const auto b=blend_basis(rotation(face.poses[expression.poses[2]][i]),rotation(face.poses[expression.poses[3]][i]),second);
        auto local=blend_basis(a,b,mix);local.columns[3]=skeleton.bones[bone].translation;
        if(i==0){
            // 1d0898 composes only the three rotation columns, preserving the
            // animated facial root position instead of translating it twice.
            const auto translation=pose[bone].columns[3];
            pose[bone]=math::multiply(pose[bone],local);pose[bone].columns[3]=translation;
        }else{
            const auto parent=skeleton.bones[bone].parent;
            require(parent>=0 && unsigned(parent)<bone,"Facial bone has invalid parent");
            pose[bone]=math::multiply(pose[unsigned(parent)],local);
        }
    }
}
std::vector<math::Matrix4> sample_skeleton(const Skeleton& skeleton,const SkeletalClip& clip,int index) {
    return sample_skeleton(skeleton,clip,float(index),RootMotion::full);
}
namespace {
std::vector<math::Matrix4> sample_local(const Skeleton& skeleton,const SkeletalClip& clip,float position,RootMotion root,unsigned channel_offset=0,bool attached=false) {
    require(std::isfinite(position),"Non-finite animation frame");
    clip.frame(0); // Also rejects a retained unsupported legacy clip.
    require(!skeleton.bones.empty(),"Empty skeleton");
    const auto clamped=std::clamp(position,0.f,float(clip.frames.size()-1));
    const int index=int(clamped);const float fraction=clamped-float(index);
    const auto& frame=clip.frame(index);const auto& next=clip.frame(index+1);
    auto translation=frame.root_motion*(1-fraction)+next.root_motion*fraction;
    switch(root) {
    case RootMotion::full: translation=translation+clip.root_origin;break;
    case RootMotion::height_only: translation={0,translation.y+clip.root_origin.y,0,1};break;
    case RootMotion::disabled: translation={0,0,0,1};break;
    default: throw std::runtime_error("Invalid root-motion policy");
    }
    translation.w=1;
    if(attached){
        // 1df9f8 compares each attachment channel with the parent clip's
        // channel count. Missing trailing channels take the identity path;
        // body-only combat clips therefore leave an attached weapon in its
        // bind pose instead of making the clip invalid.
        translation=skeleton.bones.front().translation;translation.w=1;
    }
    // Clear mask bits, including omitted trailing channels, use identity.
    std::vector<math::Matrix4> pose;pose.reserve(skeleton.bones.size());
    for(std::size_t i=0;i<skeleton.bones.size();++i) {
        const auto& bone=skeleton.bones[i];
        if(bone.parent==-2) {pose.push_back(math::identity());continue;}
        const auto channel=i+channel_offset;
        const auto a=rotation(channel<frame.rotations.size()?frame.rotations[channel]:BoneAngles{});
        const auto b=rotation(channel<next.rotations.size()?next.rotations[channel]:BoneAngles{});
        auto local=blend_basis(a,b,fraction);
        if(i==0) {require(bone.parent==-1,"Invalid skeleton root");local.columns[3]=translation;}
        else {require(bone.parent>=0 && std::size_t(bone.parent)<i,"Invalid skeleton parent");local.columns[3]=bone.translation;local.columns[3].w=1;}
        pose.push_back(local);
    }
    return pose;
}
std::vector<math::Matrix4> to_world(const Skeleton& skeleton,std::vector<math::Matrix4> pose){
    for(std::size_t i=0;i<pose.size();++i)if(skeleton.bones[i].parent>=0)pose[i]=math::multiply(pose[std::size_t(skeleton.bones[i].parent)],pose[i]);
    return pose;
}
}
std::vector<math::Matrix4> sample_skeleton(const Skeleton& skeleton,const SkeletalClip& clip,float position,RootMotion root){return to_world(skeleton,sample_local(skeleton,clip,position,root));}
std::vector<math::Matrix4> sample_attachment_skeleton(const Skeleton& skeleton,const SkeletalClip& clip,float frame,unsigned channel){return to_world(skeleton,sample_local(skeleton,clip,frame,RootMotion::disabled,channel,true));}
std::vector<math::Matrix4> blend_attachment_skeleton(const Skeleton& skeleton,const SkeletalClip& from,float from_frame,const SkeletalClip& to,float to_frame,float weight,unsigned channel){
    require(std::isfinite(weight) && weight>=0 && weight<=1,"Invalid attachment blend weight");
    auto pose=sample_local(skeleton,from,from_frame,RootMotion::disabled,channel,true);const auto next=sample_local(skeleton,to,to_frame,RootMotion::disabled,channel,true);
    for(std::size_t i=0;i<pose.size();++i){const auto translation=pose[i].columns[3];pose[i]=blend_basis(pose[i],next[i],weight);pose[i].columns[3]=translation;}
    return to_world(skeleton,std::move(pose));
}
std::vector<math::Matrix4> blend_skeleton(const Skeleton& skeleton,const SkeletalClip& from,float from_frame,const SkeletalClip& to,float to_frame,float weight,RootMotion root){
    require(std::isfinite(weight) && weight>=0 && weight<=1,"Invalid skeletal blend weight");
    auto pose=sample_local(skeleton,from,from_frame,root);const auto next=sample_local(skeleton,to,to_frame,root);
    for(std::size_t i=0;i<pose.size();++i){const auto translation=pose[i].columns[3]*(1-weight)+next[i].columns[3]*weight;pose[i]=blend_basis(pose[i],next[i],weight);pose[i].columns[3]=translation;}
    return to_world(skeleton,std::move(pose));
}
}
