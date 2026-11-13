#pragma once
#include "script/runtime.hpp"
#include <array>

namespace lifeline::script {
// Four original script-owned quadratic/cubic Bezier handles (1142e8).
class Curves {
    struct Curve {unsigned degree=0;std::array<std::array<float,4>,4> points{};};
    std::array<Curve,4> curves_{};
public:
    Bindings bindings();
};
}
