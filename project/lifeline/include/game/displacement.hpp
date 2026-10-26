#pragma once
#include "core/math.hpp"
#include <optional>
#include <utility>

namespace lifeline {
// Two retained controller vectors: 1c5ca8 updates them; 1c64e8 or
// 2b16d8 applies blend weights in place depending on movement suppression.
class AnimationDisplacement {
    math::Vector4 current_{},previous_{};
    void weight(float blend){current_=current_*blend;previous_=previous_*(1-blend);}
public:
    void select(){std::swap(current_,previous_);}
    math::Vector4 advance(math::Vector4 current,std::optional<math::Vector4> previous,float blend,bool suppressed){
        current_=current;if(previous)previous_=*previous;
        if(!suppressed)weight(blend);
        return current_+previous_;
    }
    float horizontal(float blend,bool suppressed){
        if(suppressed)weight(blend);
        const auto sum=current_+previous_;
        return std::sqrt(sum.x*sum.x+sum.z*sum.z);
    }
};
}
