#pragma once
#include "rendering/renderer.hpp"
namespace lifeline {
std::unique_ptr<SceneRenderer> make_d3d12_renderer(bool debug=false,unsigned supersample=2);
}
