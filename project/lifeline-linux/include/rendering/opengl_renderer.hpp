#pragma once
#include "rendering/renderer.hpp"
namespace lifeline {
std::unique_ptr<SceneRenderer> make_opengl_renderer();
}
