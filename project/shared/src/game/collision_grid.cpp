#include "game/collision_grid.hpp"
#include <bit>
#include <algorithm>
#include <stdexcept>
namespace lifeline {
void CollisionGrid::load(Bytes bytes){
    CollisionGrid next;next.bytes_=std::move(bytes);
    const auto word=[&](unsigned at){if(std::uint64_t(at)+4>next.bytes_.size())throw std::runtime_error("Truncated room collision grid");return le32(next.bytes_.data()+at);};
    const auto number=[&](unsigned at){const float v=std::bit_cast<float>(word(at));if(!std::isfinite(v))throw std::runtime_error("Nonfinite room collision grid field");return v;};
    next.horizontal_=number(0);next.vertical_=number(4);next.width_=number(16);next.depth_=number(20);
    next.columns_=word(36);next.rows_=word(40);next.levels_=word(44);next.height_columns_=word(8);next.height_rows_=word(12);
    next.occupancy_=word(52)&~3u;next.heights_=word(56)&~3u;
    next.minimum_={number(24),number(60),number(28),1};
    next.maximum_=next.minimum_+math::Vector4{next.width_,float(next.levels_)*next.vertical_,next.depth_,0};
    if(next.horizontal_<=0||next.vertical_<=0||next.width_<=0||next.depth_<=0||!next.columns_||!next.rows_||!next.levels_||next.levels_%8||!next.height_columns_||!next.height_rows_)
        throw std::runtime_error("Invalid room collision grid dimensions");
    const auto volume=(std::uint64_t(next.columns_)+1)*(std::uint64_t(next.rows_)+1);
    const auto samples=std::uint64_t(next.height_columns_)*next.height_rows_;
    if(volume>next.bytes_.size() || samples>next.bytes_.size()/4 || std::uint64_t(next.occupancy_)+volume*(next.levels_/8)>next.bytes_.size() || std::uint64_t(next.heights_)+samples*4>next.bytes_.size())
        throw std::runtime_error("Room collision grid arrays exceed installed resource");
    *this=std::move(next);
}
float CollisionGrid::height(math::Vector4 p)const{
    for(float v:{p.x,p.y,p.z})if(!std::isfinite(v))throw std::runtime_error("Invalid room collision height query");
    if(!loaded() || p.x<minimum_.x||p.y<minimum_.y||p.z<minimum_.z||p.x>maximum_.x||p.y>maximum_.y||p.z>maximum_.z)return p.y;
    const auto x=unsigned((p.x-minimum_.x)/(width_/float(height_columns_)));
    const auto z=unsigned((p.z-minimum_.z)/(depth_/float(height_rows_)));
    const auto index=std::uint64_t(z)*height_columns_+x;
    // Original upper-bound comparison is inclusive; reject the out-of-array
    // endpoint instead of reading the following allocation.
    if(index>=std::uint64_t(height_columns_)*height_rows_)return p.y;
    return std::bit_cast<float>(le32(bytes_.data()+heights_+index*4));
}
std::optional<CollisionGrid::Hit> CollisionGrid::segment(math::Vector4 first,math::Vector4 second)const{
    if(!loaded())return {};
    for(float v:{first.x,first.y,first.z,second.x,second.y,second.z})if(!std::isfinite(v))throw std::runtime_error("Invalid room collision segment");
    const auto origin=first;
    // 14e2e0..14e720 clips both endpoints, in X/Y/Z order.
    const auto clip=[&](float a,float b,float bound,bool lower){
        const bool out_a=lower?a<bound:a>bound,out_b=lower?b<bound:b>bound;
        if(out_a&&out_b)return false;
        if(out_a){const float denominator=b-a;first=first*((b-bound)/denominator)+second*((bound-a)/denominator);}
        else if(out_b){const float denominator=b-a;second=second*((bound-a)/denominator)+first*((b-bound)/denominator);}
        return true;
    };
    if(!clip(first.x,second.x,minimum_.x,true)||!clip(first.x,second.x,maximum_.x,false)||!clip(first.y,second.y,minimum_.y,true)||!clip(first.y,second.y,maximum_.y,false)||!clip(first.z,second.z,minimum_.z,true)||!clip(first.z,second.z,maximum_.z,false))return {};
    const auto delta=second+first*(-1);const float x=std::abs(delta.x),y=std::abs(delta.y),z=std::abs(delta.z);
    // Strict comparisons and the Z tie fallback are original, including a
    // miss for sub-cell segments. This is not a replacement DDA algorithm.
    const float count=x>y&&x>z?x/horizontal_:y>x&&y>z?y/vertical_:z/horizontal_;
    if(count<1)return {};if(!std::isfinite(count)||count>float(bytes_.size()))throw std::runtime_error("Invalid room collision sample count");
    const unsigned steps=unsigned(count);const auto step=delta*(1.f/float(steps));auto p=first;
    for(unsigned i=0;i<steps;++i,p=p+step){
        const int ix=int((p.x-minimum_.x)/horizontal_),iy=int((p.y-minimum_.y)/vertical_),iz=int((p.z-minimum_.z)/horizontal_);
        if(ix<0||iz<0||iy<0||unsigned(ix)>columns_||unsigned(iz)>rows_||unsigned(iy)>=levels_)continue;
        const auto at=std::uint64_t(occupancy_)+(std::uint64_t(iz)*(columns_+1)+unsigned(ix))*(levels_/8)+unsigned(iy)/8;
        if(bytes_.at(std::size_t(at))&(1u<<(unsigned(iy)%8))){p.w=1;const auto d=p+origin*(-1);return Hit{p,std::sqrt(math::dot_xyz(d,d))};}
    }
    return {};
}
}
