#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include <span>
#include <optional>
namespace lifeline {
struct ShadowGeometry {
    struct Bone {int parent;math::Matrix4 inverse_bind;};
    struct Vertex {math::Vector4 position;unsigned bone;};
    std::vector<Bone> bones;
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
};
std::optional<ShadowGeometry> decode_shadow_geometry(std::span<const unsigned char> model);
std::vector<math::Vector4> pose_shadow_geometry(const ShadowGeometry&,std::span<const math::Matrix4> pose);
}
