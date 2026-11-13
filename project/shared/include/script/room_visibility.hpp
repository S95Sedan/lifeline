#pragma once
#include "script/runtime.hpp"
#include "core/math.hpp"
#include <array>

namespace lifeline::script {
// Authored solid067 room sectors, selected by the cylindrical camera position.
std::array<unsigned char,374> cylinder_room_visibility(math::Vector4 eye,FlagBank flags);
}
