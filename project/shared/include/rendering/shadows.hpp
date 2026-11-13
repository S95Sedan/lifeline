#pragma once
#include "core/math.hpp"
#include <vector>
#include <span>
namespace lifeline {
struct ShadowSettings {int mode=0;math::Vector4 direction{0,-1,-1,0};float opacity=48;};
struct ShadowCaster {math::Vector4 root;std::vector<math::Vector4> vertices;std::vector<unsigned> indices;};
struct ShadowMap {
    math::Matrix4 projection=math::identity();
    std::vector<unsigned char> mask;
    // Nearest caster depth along the normalized authored light direction.
    // Uncovered texels contain positive infinity; empty depth keeps mask-only sampling.
    std::vector<float> depth;
    // Small world-space tolerance converted to projection depth, independent of resolution.
    float depth_bias=0;
    float opacity=0;
    unsigned size=512;
    float sample(math::Vector4 world)const;
};
ShadowMap make_shadow_map(std::span<const ShadowCaster>,const ShadowSettings&,unsigned size=512);
// Reuse storage; consume the output only after successful generation.
void make_shadow_map(ShadowMap&,std::span<const ShadowCaster>,const ShadowSettings&,unsigned size=512);
}
