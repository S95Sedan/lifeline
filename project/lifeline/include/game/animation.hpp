#pragma once
#include "core/bytes.hpp"
#include "rendering/model.hpp"
#include "core/math.hpp"
#include "game/shadow_geometry.hpp"
#include <span>
#include <optional>

namespace lifeline {
struct BoneAngles {std::int16_t x{},y{},z{};};
struct SkeletalFrame {
    math::Vector4 root_motion{};
    std::vector<BoneAngles> rotations;
};
struct SkeletalClip {
    unsigned channel_count{}, source_offset{};
    std::string unsupported_reason;
    math::Vector4 root_origin{};
    std::vector<SkeletalFrame> frames;
    // Original 0x1ccf10 clamps at both ends; looping is caller policy.
    const SkeletalFrame& frame(int index) const;
};
// Original 0x1c5ca8 stepping. Mode 0 stops, mode 1 loops at frame_count-1.
class AnimationCursor {
    float position_{},last_{},speed_{};
    unsigned mode_{};
    bool playing_=true;
public:
    AnimationCursor(const SkeletalClip&,unsigned mode,float speed,float start=-1.f);
    void advance();
    float position() const {return position_;}
    bool playing() const {return playing_;}
    float speed()const{return speed_;}
    unsigned mode()const{return mode_;}
    void speed(float);
    void mode(unsigned);
};
struct AnimationBank {std::vector<SkeletalClip> clips;};
const SkeletalClip& resolve_animation_clip(const AnimationBank& shared,const AnimationBank& scene,unsigned animation_id);
AnimationBank decode_animation_bank(std::span<const unsigned char>);
struct SkeletonBone {
    int parent=-1;
    math::Vector4 translation{};
    math::Matrix4 inverse_bind=math::identity();
    std::string name;
    math::Vector4 gaze_axis{0,0,1,0};
    // Runtime SKICOM +1c; the model loader clears the body compensation mix.
    float gaze_weight=0;
};
struct FacialPoses {
    unsigned first_bone=0;
    std::vector<std::vector<BoneAngles>> poses;
};
struct FacialExpression {
    std::array<unsigned,4> poses{};
    float first_weight=0,second_weight=0,mix_weight=0;
    bool preserve_eyes=false;
};
struct Skeleton {std::vector<SkeletonBone> bones;std::optional<FacialPoses> facial;};
// Four authored FANICOM poses blended after body animation, before skinning.
void apply_facial_expression(const Skeleton&,std::span<math::Matrix4>,const FacialExpression&);
// Animated SKICOM poses replace the stored setup angles; inverse bind matrices
// retain the authored bind transform, including nonzero enemy setup rotations.
Skeleton decode_skeleton(std::span<const unsigned char>);
// Recovered per-frame root policy; selecting the actor policy is caller work.
enum class RootMotion { full, height_only, disabled };
// Original normalized-axis matrix interpolation, not Euler-angle interpolation.
std::vector<math::Matrix4> sample_skeleton(const Skeleton&,const SkeletalClip&,float frame,RootMotion root=RootMotion::full);
// Two running clips, blended in bone-local space before parent transforms.
std::vector<math::Matrix4> blend_skeleton(const Skeleton&,const SkeletalClip& from,float from_frame,const SkeletalClip& to,float to_frame,float weight,RootMotion root=RootMotion::full);
// Attached models share a parent's animation bank, using their own channel
// range and bind translations (Tutorial gun: right hand 20, channels 74..76).
std::vector<math::Matrix4> sample_attachment_skeleton(const Skeleton&,const SkeletalClip&,float frame,unsigned first_channel);
std::vector<math::Matrix4> blend_attachment_skeleton(const Skeleton&,const SkeletalClip&,float,const SkeletalClip&,float,float weight,unsigned first_channel);
// Integer-frame compatibility entry point (full root motion).
std::vector<math::Matrix4> sample_skeleton(const Skeleton&,const SkeletalClip&,int frame);
struct SkinVertex {
    math::Vector4 position{},normal{};
    std::array<int,3> bones{-1,-1,-1};
    std::array<float,3> weights{};
    float u{},v{},light{},alpha{};
    unsigned strip_flags{};
};
struct SkinMesh {
    unsigned texture{},flags{},material_parameter{};
    std::vector<SkinVertex> vertices;
    std::vector<unsigned> indices;
};
struct SkinnedGeometry {std::vector<SkinMesh> meshes;bool planar_reflection=false;std::optional<ShadowGeometry> shadow;};
StaticModel decode_skin_surface(std::span<const unsigned char>,const SkinnedGeometry&);
void pose_skin_surface(StaticModel&,const SkinnedGeometry&,const Skeleton&,std::span<const math::Matrix4>);
SkinnedGeometry decode_skinned_geometry(std::span<const unsigned char>,const Skeleton&);
// CPU skinning into ordinary model-space vertex positions. No console execution.
std::vector<math::Vector4> skin_positions(const SkinMesh&,const Skeleton&,std::span<const math::Matrix4> pose);
}
