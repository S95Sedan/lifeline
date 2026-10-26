#pragma once
#include "script/runtime.hpp"
namespace lifeline::script {
struct BattleTargetHelper {unsigned kind=0,table=0;};
std::optional<BattleTargetHelper> identify_battle_target_helper(std::span<const std::uint32_t> words,unsigned entry);
float battle_target_helper(const BattleTargetHelper&,const Program&,const State&,std::span<const float> arguments);
}
