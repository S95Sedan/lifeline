#pragma once
namespace lifeline::script {
// Native result service for adapted title helpers. The original distinguishes
// player interruption (return 1) from ordinary movie completion (return 0).
inline constexpr unsigned movie_skip_result_opcode=1203;
}
