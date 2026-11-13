#pragma once
#include "assets/flash.hpp"
#include <SDL3/SDL.h>
#include <limits>
#include <stdexcept>
#include <vector>

namespace lifeline {
// Submit consecutive draws together without changing triangle or blend order.
// Flush before changing the render target, viewport, clipping, or texture state.
class GeometryBatch {
    SDL_Renderer* renderer_;
    SDL_Texture* texture_=nullptr;
    unsigned address_=0;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
public:
    explicit GeometryBatch(SDL_Renderer* renderer):renderer_(renderer){}
    void flush(){
        const bool ok=indices_.empty()||SDL_RenderGeometry(renderer_,texture_,vertices_.data(),int(vertices_.size()),indices_.data(),int(indices_.size()));
        vertices_.clear();indices_.clear();texture_=nullptr;address_=0;
        if(!ok)throw std::runtime_error(SDL_GetError());
    }
    template<class Point> void append(SDL_Texture* texture,const FlashDraw& draw,Point point){
        if(draw.indices.empty())return;
        constexpr auto limit=std::size_t(std::numeric_limits<int>::max());
        if(draw.vertices.size()>limit||draw.indices.size()>limit)throw std::runtime_error("Interface geometry exceeds native limits");
        for(int index:draw.indices)if(index<0||std::size_t(index)>=draw.vertices.size())throw std::runtime_error("Invalid interface geometry index");
        unsigned address=0;if(texture)for(const auto& v:draw.vertices){if(v.u<0||v.u>1)address|=1;if(v.v<0||v.v>1)address|=2;}
        // SDL resolves automatic wrap/clamp once per geometry call and axis.
        if(texture_!=texture||address_!=address||vertices_.size()+draw.vertices.size()>65536||indices_.size()+draw.indices.size()>limit)flush();
        texture_=texture;address_=address;const int base=int(vertices_.size());
        for(const auto& v:draw.vertices)vertices_.push_back({point(v),{v.color.x,v.color.y,v.color.z,v.color.w},{v.u,v.v}});
        for(int index:draw.indices)indices_.push_back(base+index);
    }
    void append(SDL_Texture* texture,const FlashDraw& draw){append(texture,draw,[](const FlashVertex& v){return SDL_FPoint{v.x,v.y};});}
};
}
