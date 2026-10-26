#pragma once
#include "rendering/model.hpp"
#include "rendering/quality.hpp"
#include <memory>
#include <string_view>

namespace lifeline {
// Backend-neutral scene submission. References remain valid for the synchronous
// render call; a backend retaining work must copy/upload what it needs.
struct SceneSubmission {
    const StaticModel& model;
    const SceneCamera& camera;
    std::span<const unsigned char> visibility{};
    bool transparent=false;
    bool high_resolution=false;
};

class SceneRenderer {
public:
    virtual ~SceneRenderer()=default;
    virtual std::string_view name()const noexcept=0;
    // Reference callers receive 640x448. High-resolution callers use the full
    // drawable size and extend the frustum without changing camera magnification.
    virtual AssetImage render(const SceneSubmission&)=0;
    // Reuse caller-owned storage when supported. Consume it only after success.
    virtual void render_into(AssetImage& output,const SceneSubmission& submission){output=render(submission);}
};

// Set once during application startup; all subsequent game sessions inherit it.
enum class RendererBackend {software,d3d12};
void select_renderer_backend(std::string_view name);
RendererBackend renderer_backend();
// Use the whole drawable; preserve focal length and extend the camera frustum.
void set_scene_drawable_size(unsigned width,unsigned height);
std::pair<unsigned,unsigned> scene_render_size();
void set_scene_quality(unsigned render_scale,unsigned texture_filter,const GraphicsQuality& quality={});
unsigned scene_texture_filter();
std::unique_ptr<SceneRenderer> make_scene_renderer();
std::unique_ptr<SceneRenderer> make_software_renderer();
AssetImage render_model(const StaticModel&,const SceneCamera& camera={},unsigned node_mask=3,bool transparent=false);
AssetImage render_scene(const StaticModel&,const SceneCamera&,std::span<const unsigned char> visibility={},bool transparent=false,unsigned width=640,unsigned height=448,std::vector<float>* output_depth=nullptr,Bytes* output_eligible=nullptr,SceneTextureCache* texture_cache=nullptr);
// Explicit hardware-only selection: unsupported devices throw, never use WARP.
// The debug layer is optional for development checks, not required to play.
std::unique_ptr<SceneRenderer> make_d3d12_renderer(bool debug=false,unsigned supersample=2);
}
