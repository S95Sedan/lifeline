#pragma once
#include "assets/flash.hpp"
#include <limits>

namespace lifeline {
inline void prepare_title_exit_art(FlashMovie& movie){
    if(movie.title_exit_mask||movie.name!="tit_load.cf")return;
    const auto shifted=[](const AssetImage& source,float offset){
        AssetImage result=source;result.name="Native title selection mask";
        for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){
            const float sample=float(y)-offset;const int row=int(std::floor(sample));const float fraction=sample-std::floor(sample);
            const auto alpha=[&](int sy){return sy<0||sy>=int(source.height)?254.f:float(source.rgba[(unsigned(sy)*source.width+x)*4+3]);};
            result.rgba[(y*source.width+x)*4+3]=static_cast<unsigned char>(std::lround(alpha(row)*(1-fraction)+alpha(row+1)*fraction));
        }
        return result;
    };
    // Keep the opaque mask rectangle fixed. Moving its whole quad exposes a
    // bright rectangle in the title's destination-alpha background effect.
    for(unsigned id:{1u,3u,5u,7u,9u}){
        const auto next=movie.textures.rbegin()->first+1;
        movie.textures.emplace(next,shifted(movie.textures.at(id),-15));movie.title_menu_masks.emplace(id,next);
    }
    const auto next=movie.textures.rbegin()->first+1;
    movie.textures.emplace(next,shifted(movie.textures.at(9),15.5f));movie.title_exit_mask=next;
    AssetImage label{"Quit Game title row",132,32,Bytes(132*32*4)};
    struct Letter {unsigned texture,x,y,width;};
    constexpr Letter letters[]{{24,6,9,14},{26,59,9,13},{24,53,9,4},{24,38,9,12},
        {20,63,7,14},{20,81,7,13},{20,97,7,14},{20,114,7,12}};
    unsigned left=0;
    for(unsigned i=0;i<std::size(letters);++i){
        if(i==4)left+=8;
        const auto& g=letters[i];const auto& source=movie.textures.at(g.texture);
        for(unsigned y=0;y<15;++y)for(unsigned x=0;x<g.width;++x)
            std::copy_n(source.rgba.data()+((g.y+y)*source.width+g.x+x)*4,4,label.rgba.data()+((y+9)*132+left+x)*4);
        left+=g.width+4;
    }
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<4;++x)if(8+y/2+x<14){
        auto* pixel=label.rgba.data()+((19+y)*132+8+y/2+x)*4;
        pixel[0]=pixel[1]=pixel[2]=240;pixel[3]=std::max<unsigned char>(pixel[3],x==0||x==3?64:224);
    }
    const auto label_id=movie.textures.rbegin()->first+1;
    movie.textures.emplace(label_id,std::move(label));movie.title_exit_label=label_id;
}
// Applied before both scene compositing and direct UI rendering, so the
// title's destination-alpha effects and its labels share the same layout.
inline void extend_title_layout(const FlashMovie& movie,FlashDraw& draw,bool quit_selected){
    float offset=0;
    if(movie.name=="tit_load.cf"&&draw.textured){
        if(draw.texture==20||draw.texture==22||draw.texture==24||draw.texture==26)offset=-15;
        else if(const auto found=movie.title_menu_masks.find(draw.texture);found!=movie.title_menu_masks.end())
            draw.texture=quit_selected&&draw.texture==9?*movie.title_exit_mask:found->second;
    }else if(movie.name=="tit_loadcur.cf")offset=-15+(quit_selected?30.5f:0);
    for(auto& vertex:draw.vertices)vertex.y+=offset;
}
inline void extend_title_layout(const FlashMovie& movie,std::vector<FlashDraw>& draws,bool quit_selected){
    std::optional<FlashDraw> quit;
    for(auto& draw:draws){
        if(movie.title_exit_label&&draw.textured&&draw.texture==26){
            quit=draw;quit->texture=*movie.title_exit_label;
            float low=std::numeric_limits<float>::max(),high=-low;
            for(const auto& v:draw.vertices){low=std::min(low,v.x);high=std::max(high,v.x);}
            const float center=(low+high)*.5f;
            for(auto& v:quit->vertices){v.x=center+(v.x-center)*132.f/96.f;v.y+=15.5f;}
        }
        extend_title_layout(movie,draw,quit_selected);
    }
    // Clone Bonus's actual draw, including its authored reveal opacity.
    if(quit)draws.push_back(std::move(*quit));
}
}
