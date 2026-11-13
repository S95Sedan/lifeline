#include "rendering/d3d12_renderer.hpp"
#include "platform/host.hpp"
namespace lifeline::platform {
std::unique_ptr<SceneRenderer> make_renderer(RendererBackend backend){
    if(backend==RendererBackend::d3d12)return make_d3d12_renderer();
    throw std::runtime_error("OpenGL rendering was not enabled in this Windows build");
}
}
