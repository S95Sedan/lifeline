#pragma once
#include "rendering/image.hpp"
#include "core/math.hpp"
#include "rendering/lighting.hpp"
#include "rendering/shadows.hpp"
#include <limits>
#include <optional>

namespace lifeline {
struct ModelVertex {
    math::Vector4 position, normal, color;
    float u{}, v{};
};
struct ModelRefraction {
    unsigned strength=0; // GS fixed alpha, 0..128.
    float normal_offset=0;
    math::Vector4 tint{1,1,1,1}; // Authored RGB bytes divided by 128.
};
struct SkinMaterial {
    math::Vector4 diffuse{1,1,1,0},ambient_extra{},specular{},emission{};
    float power=1,diffuse_gain=1;
};
struct ModelMesh {
    unsigned node{}, texture{}, stream{}, flags{};
    std::vector<ModelVertex> vertices;
    std::vector<unsigned> indices;
    // Original character refraction uses a captured scene, displaced screen
    // coordinates and Q=1 (affine sampling), rather than the outfit's UVs.
    bool captured_scene=false;
    // Effect pass 8 samples one shared 256x256 scene copy with authored UVs.
    bool scene_wave=false;
    std::optional<std::array<unsigned,4>> effect_blend; // (source/destination/zero difference) * source/destination alpha + color.
    bool effect_pass=false; // Live effect pass 8, in manager slot order.
    float normal_offset=0;
    ModelRefraction refraction;
    // Coplanar UI layers test against the room but retain authored draw order.
    bool depth_write=true;
    bool opaque_alpha_depth=false; // Opaque fragments of alpha-blended character surfaces still occlude effects.
    bool depth_test=true;
    bool world_surface=false; // Depth-tested screen surface, before translucent actors/effects.
    SkinMaterial skin_material;
    bool scene_lighting=false;
    bool receives_shadow=false;
    // Native geometric overlay classification; zero uses ordinary depth ordering.
    // Nonzero IDs are scoped by node: renderers must use (node, layer_group).
    unsigned layer_group=0;
    math::Vector4 lighting_position{};
    bool repeat_u=false,repeat_v=false;
    bool linear_filter=false;
    bool background=false; // Original effect pass 0, before room geometry, with ZTE disabled.
    int fog_mode=1; // 0 disables; 1 uses camera; other values use node settings.
    float fog_near=0,fog_far=50;
    math::Vector4 fog_color{128,0,0,0};
    std::optional<std::array<math::Vector4,8>> culling_corners;
    bool reflection_pass=false; // Additional mirrored geometry, not the source surface.
};
struct StaticModel {
    std::vector<AssetImage> textures;
    std::vector<ModelMesh> meshes;
    unsigned nodes{};
    std::vector<SceneLight> lights;
    ShadowMap shadow;
};
// Native SOLCOM0418/0419 meshes and independently indexed texture pages.
// Produces native meshes; does not execute console graphics commands.
StaticModel decode_static_model(std::span<const unsigned char> bytes);
math::Matrix4 reflection_matrix(math::Vector4 normal,math::Vector4 point);
// Skinned surfaces use the renderer's inverted Y axis. Script planes do not.
ModelMesh reflect_skin_mesh(const ModelMesh&,const math::Matrix4& authored_plane);
struct SceneCamera {
    math::Vector4 eye{-.035f,-7.445f,-41.953f,1};
    math::Vector4 target{.118f,-5.811f,-32.089f,1};
    float focal_pixels=544, near_plane=.1f;
    bool mirror_horizontal=false;
    math::Vector4 up{0,-1,0,0};
    math::Vector4 clear_color{0,0,0,0}; // RGB bytes and the authored clear enable.
    math::Vector4 fill_light{40,40,40,0}; // Native camera +180: RGB and forced fill enable.
    float far_plane=std::numeric_limits<float>::infinity();
    bool fog_enabled=false;
    float fog_near=50, fog_far=100;
    math::Vector4 fog_color{0,0,64,0};
    bool reflection_enabled=false;
    ShadowSettings shadow;
    // Camera constructor 1ca108..1ca148: normal (-1,0,0), point (50,0,0).
    math::Matrix4 reflection{{math::Vector4{-1,0,0,0},math::Vector4{0,1,0,0},math::Vector4{0,0,1,0},math::Vector4{100,0,0,1}}};
};
}
