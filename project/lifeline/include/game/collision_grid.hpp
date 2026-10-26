#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include <optional>
namespace lifeline {
// Authored AT occupancy/height data; no dependency on model render geometry.
class CollisionGrid {
    Bytes bytes_;
    math::Vector4 minimum_{},maximum_{};
    float horizontal_=0,vertical_=0,width_=0,depth_=0;
    unsigned columns_=0,rows_=0,levels_=0,height_columns_=0,height_rows_=0;
    unsigned occupancy_=0,heights_=0;
public:
    struct Hit {math::Vector4 point;float distance;};
    void load(Bytes);
    void reset(){*this=CollisionGrid{};}
    bool loaded()const{return !bytes_.empty();}
    float height(math::Vector4)const;
    std::optional<Hit> segment(math::Vector4 first,math::Vector4 second)const;
};
}
