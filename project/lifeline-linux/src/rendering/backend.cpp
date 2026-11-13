#include "rendering/opengl_renderer.hpp"
#include "platform/host.hpp"
namespace lifeline::platform {
std::unique_ptr<SceneRenderer> make_renderer(RendererBackend backend){
    if(backend==RendererBackend::opengl)return make_opengl_renderer();
    throw std::runtime_error("D3D12 hardware rendering is available only on Windows");
}
}
