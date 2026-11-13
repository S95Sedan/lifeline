#pragma once
#include "script/runtime.hpp"
#include "ui/combat_targets.hpp"
namespace lifeline {class CombatPanel;}
namespace lifeline::script {
Bindings combat_target_bindings(CombatTargets&,CombatPanel* panel=nullptr);
}
