#include "ui/font.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <sstream>
#include <memory>

namespace lifeline {
Font::Font(const AssetDirectory& assets){
    const auto bytes=assets.read("ui/fonts/dialogue/font.tsv");
    std::istringstream stream(std::string(bytes.begin(),bytes.end()));std::string tag;unsigned version,size;
    if(!(stream>>tag>>version) || tag!="LIFELINE_FONT" || version!=1 || !(stream>>tag>>size) || tag!="size" || size!=22)
        throw std::runtime_error("Invalid installed dialogue font map");
    using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
    Surface source(SDL_LoadBMP(assets.resolve("ui/fonts/dialogue/atlas.bmp").string().c_str()),SDL_DestroySurface);
    if(!source)throw std::runtime_error("Cannot load installed dialogue font atlas");
    Surface rgba(SDL_ConvertSurface(source.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    if(!rgba || rgba->w!=1024 || rgba->h!=880)throw std::runtime_error("Invalid dialogue font atlas dimensions");
    atlas_={"dialogue",unsigned(rgba->w),unsigned(rgba->h),Bytes(std::size_t(rgba->w)*rgba->h*4)};
    for(int y=0;y<rgba->h;++y)std::copy_n(static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4,atlas_.rgba.data()+std::size_t(y)*rgba->w*4);
    while(stream>>tag){unsigned code,x,y;if(tag!="glyph" || !(stream>>code>>x>>y) || x+22>atlas_.width || y+22>atlas_.height || !glyphs_.emplace(code,std::array{x,y}).second)
        throw std::runtime_error("Invalid dialogue glyph entry");}
}
void Font::draw(AssetImage& image,unsigned code,float x,float y,float size,const std::array<unsigned,4>& color,const std::array<int,4>& clip)const{
    const auto glyph=glyphs_.find(code);if(glyph==glyphs_.end() || size<=0)return;
    const int left=std::max({int(std::ceil(x)),clip[0],0}),top=std::max({int(std::ceil(y)),clip[1],0});
    const int right=std::min({int(std::ceil(x+size)),clip[2],int(image.width)}),bottom=std::min({int(std::ceil(y+size)),clip[3],int(image.height)});
    for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
        // The original sprite samples UV 1..22 of each 22-pixel cell.
        const unsigned sx=glyph->second[0]+std::min(21u,unsigned(std::max(0.f,(px+.5f-x)/size*21.f+1.f)));
        const unsigned sy=glyph->second[1]+std::min(21u,unsigned(std::max(0.f,(py+.5f-y)/size*21.f+1.f)));
        const unsigned alpha=std::min(255u,unsigned(atlas_.rgba[(std::size_t(sy)*atlas_.width+sx)*4+3])*color[3]/128);
        auto* out=image.rgba.data()+(std::size_t(py)*image.width+px)*4;
        for(unsigned c=0;c<3;++c)out[c]=static_cast<unsigned char>((std::min(255u,color[c]*2)*alpha+out[c]*(255-alpha)+127)/255);
    }
}
}
