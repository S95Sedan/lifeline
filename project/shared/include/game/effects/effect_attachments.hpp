#pragma once
#include "game/objects.hpp"

namespace lifeline { struct SceneCamera; }

namespace lifeline::script {
math::Matrix4 effect_attachment(const Objects&,int source,int part);
math::Matrix4 effect_attachment(const Objects&,const SceneCamera&,int source,int part);
}
