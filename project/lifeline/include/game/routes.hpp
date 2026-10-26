#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include <array>
#include <algorithm>

namespace lifeline {
// Installed RS routes, including direction-encoded connection queues.
class Routes {
    Bytes data_;
    int route_=0,previous_=-1,point_=0,segment_=-1;
    int search_limit_=48;
    bool reverse_=false;
    std::array<int,48> queue_;
    unsigned read_=0,write_=0;
    std::size_t record(unsigned header,int index)const;
    std::size_t point_record(unsigned index)const;
    unsigned word(std::size_t offset)const;
    unsigned count()const;
    unsigned point_id()const;
    void update_segment();
    void update_previous();
public:
    Routes(){clear_queue();}
    void load(Bytes value){data_=std::move(value);}
    int route()const{return route_;}
    int point()const{return point_;}
    int segment()const{return segment_;}
    int previous()const{return previous_;}
    bool reverse()const{return reverse_;}
    math::Vector4 position(unsigned point)const;
    math::Vector4 target()const{return position(point_id());}
    math::Vector4 travel_direction()const;
    void select(int route,int direction,int point,bool start,bool preserve_previous);
    void advance(unsigned random_bit);
    void retreat();
    // Returns true only for the same-route reversal path, whose arrival
    // distance check is performed by the actor controller.
    bool search(int route,bool reverse);
    void search_limit(int value){search_limit_=std::min(value,48);}
    void clear_queue(){queue_.fill(-1);read_=write_=0;}
    void enqueue(int route,int direction);
    int queued()const{return read_<queue_.size()?queue_[read_]:-1;}
    int endpoint_event()const;
};
}
