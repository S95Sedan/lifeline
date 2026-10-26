#pragma once
#include "core/bytes.hpp"
#include <optional>

namespace lifeline::script {
// Authored ME action-event lookup (1acfa0). This does not execute events.
class ActionTable {
    Bytes bytes_;
    std::size_t segments_=0;
    unsigned modes_=0,segment_count_=0;
    int shared_base_=0,scene_base_=0;
    unsigned word(std::size_t)const;
    int number(std::size_t)const;
    std::optional<int> lookup(std::size_t base,unsigned index,int command,int mode,bool segment)const;
public:
    void load(Bytes);
    void shared_base(int value){shared_base_=value;}
    void scene_base(int value){scene_base_=value;}
    int event(int mode,int segment,int command)const;
};
}
