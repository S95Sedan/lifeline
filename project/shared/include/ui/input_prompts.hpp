#pragma once
#include "assets/flash.hpp"
#include <string_view>
namespace lifeline {
void prepare_title_prompt(FlashMovie&,const FlashMovie& font_source);
void prepare_continue_prompt(FlashMovie&,const FlashMovie& font_source);
void prepare_navigation_prompt(FlashMovie&,const FlashMovie& font_source);
FlashDraw movie_skip_prompt(const FlashMovie& font_source,math::Vector4 bounds);
std::optional<FlashDraw> input_prompt_shape(const FlashMovie&,unsigned shape);
std::string input_prompt_text(std::string_view variable,const std::string& source);
}
