#pragma once
#include "assets/flash.hpp"

namespace lifeline {
inline constexpr math::Vector4 settings_teal{62.f/255,196.f/255,178.f/255,1};
inline constexpr math::Vector4 settings_white{.9f,1,1,1};

// Caption lettering keeps the authored font's metrics and ink bounds.
unsigned add_settings_caption_font(FlashMovie&,unsigned body_font);
std::string settings_caption(std::string);
FlashDraw caption_geometry(std::string,math::Vector4 bounds,math::Vector4 color);
}
