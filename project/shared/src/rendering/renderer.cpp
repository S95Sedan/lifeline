#include "rendering/renderer.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cmath>
namespace lifeline {
namespace {RendererBackend selected=RendererBackend::software;std::pair<unsigned,unsigned> render_size{};unsigned render_scale=100,texture_filter=0;GraphicsQuality graphics_quality;}
void set_scene_drawable_size(unsigned width,unsigned height){
    if(!width||!height||width>7680||height>7680)throw std::runtime_error("Invalid scene drawable size");
    render_size={width,height};
}
std::pair<unsigned,unsigned> scene_render_size(){if(!render_size.first)return render_size;const auto cap=std::min(1.0,7680.0/(std::max(render_size.first,render_size.second)*render_scale/100.0));return {unsigned(std::max(1.0,std::round(render_size.first*render_scale/100.0*cap))),unsigned(std::max(1.0,std::round(render_size.second*render_scale/100.0*cap)))};}
void set_scene_quality(unsigned scale,unsigned filter,const GraphicsQuality& quality){if(scale<50||scale>200||filter>2||quality.texture_quality>2||quality.shadow_quality>4||quality.anti_aliasing>1||quality.visual_style>4||quality.reflections>2)throw std::runtime_error("Invalid graphics quality");
    scale=std::min(scale,platform::profile().max_render_scale);
    render_scale=scale;texture_filter=filter;graphics_quality=quality;}
const GraphicsQuality& scene_graphics_quality(){return graphics_quality;}
unsigned scene_texture_filter(){return texture_filter;}
void select_renderer_backend(std::string_view name){
    if(name=="software")selected=RendererBackend::software;
    else if(name=="d3d12")selected=RendererBackend::d3d12;
    else if(name=="opengl")selected=RendererBackend::opengl;
    else throw std::runtime_error("Unknown renderer: "+std::string(name)+" (use software, d3d12 or opengl)");
}
RendererBackend renderer_backend(){return selected;}
std::unique_ptr<SceneRenderer> make_scene_renderer(){
    if(selected==RendererBackend::software)return make_software_renderer();
    return platform::make_renderer(selected);
}
}
