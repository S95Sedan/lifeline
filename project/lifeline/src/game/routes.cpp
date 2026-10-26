#include "game/routes.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace lifeline {
unsigned Routes::word(std::size_t offset)const{
    if(offset>data_.size() || data_.size()-offset<4)throw std::runtime_error("Route extent outside installed data");
    return le32(data_.data()+offset);
}
std::size_t Routes::record(unsigned header,int index)const{
    if(word(0)!=0x20207372 || word(4)!=0x20000)throw std::runtime_error("Unsupported installed route format");
    if(index<0||unsigned(index)>=word(8))throw std::runtime_error("Route index outside installed data");
    const auto table=std::size_t(word(header))*4;
    return table+std::size_t(word(table+std::size_t(index)*4))*4;
}
std::size_t Routes::point_record(unsigned index)const{
    if(word(0)!=0x20207372 || word(4)!=0x20000 || index>=word(16))throw std::runtime_error("Route point outside installed data");
    return std::size_t(word(20))*4+std::size_t(index)*64;
}
unsigned Routes::count()const{
    const auto result=word(record(28,route_));
    if(result>data_.size()/4)throw std::runtime_error("Selected route has invalid point count");
    return result;
}
unsigned Routes::point_id()const{
    if(point_<0)throw std::runtime_error("Negative installed route point");
    return word(record(28,route_)+4+std::size_t(point_)*4);
}
int Routes::endpoint_event()const{
    if(point_!=(reverse_?0:int(count())-1))return -1;
    const auto p=point_record(point_id());if(!(word(p+16)&0x08000000))return -1;
    return int(std::bit_cast<std::int16_t>(std::uint16_t(word(p+28))));
}
math::Vector4 Routes::position(unsigned index)const{
    const auto p=point_record(index);
    math::Vector4 value{std::bit_cast<float>(word(p)),std::bit_cast<float>(word(p+4)),std::bit_cast<float>(word(p+8)),1};
    if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw std::runtime_error("Non-finite installed route point");
    return value;
}
void Routes::update_previous(){
    if(point_!=(reverse_?int(count())-1:0))return;
    const auto p=point_record(point_id());const auto flags=word(p+16);
    for(unsigned i=0;i<4;++i)if(flags&(0x80000000u>>i)){
        const auto packed=word(p+20+(i/2)*4);
        previous_=int(((packed>>((i%2)*16))&0xffff)^0x8000);return;
    }
}
math::Vector4 Routes::travel_direction()const{
    const auto n=int(count());if(n<2)return {};
    const int next=std::clamp(point_+(reverse_?-1:1),0,n-1);
    const int from=next==point_?std::clamp(point_+(reverse_?1:-1),0,n-1):point_;
    const auto point=[&](int i){return position(word(record(28,route_)+4+std::size_t(i)*4));};
    return point(next)+point(from)*(-1);
}
void Routes::select(int route,int direction,int point,bool start,bool preserve_previous){
    if(preserve_previous)previous_=route_|(reverse_?0x8000:0);
    if(route!=-1)route_=route;
    if(direction!=-1)reverse_=direction!=0;
    if(start)point_=reverse_?int(count())-1:0;
    else if(point!=-1)point_=point;
    (void)target();clear_queue();
    if(!preserve_previous)segment_=-1;
    update_previous();
}
void Routes::enqueue(int route,int direction){
    if(write_>=queue_.size())throw std::runtime_error("Route command queue capacity exceeded");
    queue_[write_++]=route|(direction?0x8000:0);
}
void Routes::update_segment(){
    // Original branch order matters for the empty route (upper bound -1).
    int from=point_+(reverse_?1:-1);
    if(from<0)from=0;else if(from>=int(count()))from=int(count())-1;
    const auto list=record(32,route_);
    const auto n=word(list);if(n>data_.size()/4)throw std::runtime_error("Invalid route segment list");
    segment_=-1;
    for(unsigned i=0;i<n;++i){
        const auto index=word(list+4+std::size_t(i)*4);
        if(index>=word(12))throw std::runtime_error("Route segment index outside installed data");
        const auto p=std::size_t(word(24))*4+std::size_t(index)*32;
        const int a=std::bit_cast<int>(word(p)),b=std::bit_cast<int>(word(p+4));
        if(word(p+8)!=unsigned(reverse_))continue;
        if(reverse_?(b<from && from<=a):(a<=from && from<b)){segment_=std::bit_cast<int>(word(p+12));return;}
    }
}
void Routes::advance(unsigned random_bit){
    if(point_==(reverse_?0:int(count())-1)){
        const auto p=point_record(point_id());
        previous_=route_|(reverse_?0x8000:0);
        int next=queued();
        if(next!=-1)++read_;
        else{
            const auto flags=word(p+16);int slot=-1;
            if(flags&0x20000000)slot=2;
            else if((flags&0xc0000000)==0xc0000000)slot=(random_bit&1)?0:1;
            else if(flags&0x40000000)slot=1;
            else if(flags&0x80000000)slot=0;
            else if(flags&0x10000000)slot=3;
            if(slot>=0){const auto packed=word(p+20+unsigned(slot/2)*4);next=int(std::bit_cast<std::int16_t>(std::uint16_t(packed>>((slot%2)*16))));}
            // Original logs a missing connection and selects route zero.
            if(next==-1)next=0;
        }
        route_=next&0x7fff;reverse_=((next>>15)&1)!=0;
        point_=reverse_?int(count())-1:0;
    }else point_+=reverse_?-1:1;
    update_segment();(void)target();
}
void Routes::retreat(){
    point_+=reverse_?1:-1;
    if(point_<0 || unsigned(point_)>=count()){
        const auto saved=previous_;previous_=route_|(reverse_?0:0x8000);
        route_=saved&0x7fff;reverse_=(saved>>15)!=0;
        point_=reverse_?0:int(count())-1;
    }else previous_=route_|(reverse_?0x8000:0);
    reverse_=!reverse_;clear_queue();update_segment();(void)target();
}
bool Routes::search(int destination,bool direction){
    clear_queue();
    if(route_==destination){if(reverse_!=direction){retreat();return true;}return false;}
    const bool start=point_==(reverse_?int(count())-1:0);
    std::vector<std::vector<int>> candidates;
    std::vector<int> path;
    const auto visit=[&](auto&& self,int route,bool reverse)->void{
        if(int(path.size())>=search_limit_ || candidates.size()>=8)return;
        const auto entry=record(28,route);const auto n=word(entry);
        if(!n || n>data_.size()/4)throw std::runtime_error("Invalid route in route search");
        const auto endpoint=word(entry+4+std::size_t(reverse?0:n-1)*4);
        const auto p=point_record(endpoint);const auto flags=word(p+16);
        for(unsigned slot=0;slot<4 && candidates.size()<8;++slot){
            if(!(flags&(0x80000000u>>slot)))continue;
            const auto packed=word(p+20+(slot/2)*4);
            const int next=int((packed>>((slot%2)*16))&0xffff),id=next&0x7fff;
            if(std::any_of(path.begin(),path.end(),[id](int value){return (value&0x7fff)==id;}))continue;
            path.push_back(next);
            // 12f3e8 compares route ID only, despite accepting a direction.
            if(id==destination){if(path.size()<48)candidates.push_back(path);}
            else self(self,id,(next&0x8000)!=0);
            path.pop_back();
        }
    };
    if(start)visit(visit,previous_&0x7fff,(previous_>>15)!=0);
    else visit(visit,route_,reverse_);
    const auto forward_count=candidates.size();
    if(candidates.size()<8)visit(visit,route_,!reverse_);
    if(candidates.empty())return false;
    std::size_t best=candidates.size();int length=search_limit_;
    for(std::size_t i=0;i<candidates.size();++i)if(int(candidates[i].size())<length){best=i;length=int(candidates[i].size());}
    if(best==candidates.size())throw std::runtime_error("Original route search produced no path below its depth limit");
    for(int value:candidates[best])enqueue(value,0);
    if(best>=forward_count)previous_=route_|(reverse_?0x8000:0);
    if(start){
        const auto next=queued();++read_;route_=next&0x7fff;reverse_=(next>>15)!=0;
        point_=reverse_?int(count())-1:0;update_segment();
    }else if(best>=forward_count){point_+=reverse_?1:-1;reverse_=!reverse_;update_segment();}
    (void)target();return false;
}
}
