#pragma once
#include "rendering/model.hpp"

namespace lifeline {
struct EnhancedLights {math::Vector4 key,fill,rim,view;};
EnhancedLights enhanced_lights(math::Vector4 camera_forward);
struct EnhancedView {math::Vector4 key,fill;float focal;};
EnhancedView enhanced_view(const SceneCamera&,unsigned width,unsigned height);
void apply_scene_enhanced(AssetImage&,const AssetImage& source,std::span<const float> depth,std::span<const unsigned char> eligible,const SceneCamera&);
}
