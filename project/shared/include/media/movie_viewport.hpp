#pragma once
#include "rendering/image.hpp"
#include <cmath>
#include <string_view>

namespace lifeline {
struct MovieAperture { unsigned x=0,y=0,width=0,height=0; };
struct MovieViewport { unsigned x,y,width,height; };
inline MovieAperture fullscreen_movie_aperture(std::string_view name,unsigned frame,MovieAperture authored){
    // S0 contains the letterboxed opening and subsequent full-frame footage.
    // Follow its authored opening reveal; later footage retains its aperture.
    if(name!="s0"||authored.x||authored.y||authored.width!=640||authored.height!=448)return authored;
    unsigned margin=62;
    if(frame>=2437){
        constexpr unsigned reveal[]{59,51,44,36,30,22,14,7,0};
        margin=frame<2437+std::size(reveal)?reveal[frame-2437]:0;
    }
    return {0,margin,640,448-2*margin};
}
inline MovieViewport movie_viewport(MovieAperture a,unsigned width,unsigned height){
    if(!a.width||!a.height||!width||!height)throw std::runtime_error("Invalid movie viewport");
    const double scale=std::min(double(width)/a.width,double(height)/a.height);
    const unsigned w=std::min(width,unsigned(std::lround(a.width*scale)));
    const unsigned h=std::min(height,unsigned(std::lround(a.height*scale)));
    return {(width-w)/2,(height-h)/2,w,h};
}
// Fixed aperture, uniform fit, bilinear reconstruction. Never crop live content
// to fill the window or change zoom during fades; embedded textures bypass this.
inline void render_movie(const AssetImage& source,MovieAperture a,AssetImage& destination){
    if(!a.width||!a.height||a.x+a.width>source.width||a.y+a.height>source.height||
       destination.rgba.size()!=std::size_t(destination.width)*destination.height*4)
        throw std::runtime_error("Invalid movie aperture or destination");
    const auto v=movie_viewport(a,destination.width,destination.height);
    for(std::size_t i=0;i<destination.rgba.size();i+=4){
        destination.rgba[i]=destination.rgba[i+1]=destination.rgba[i+2]=0;destination.rgba[i+3]=255;
    }
    struct Sample {unsigned low,high,weight;};
    const auto sample=[](unsigned i,unsigned size,unsigned origin,unsigned extent){
        const float p=std::clamp((i+.5f)*extent/size-.5f,0.f,float(extent-1));
        const unsigned low=unsigned(p);return Sample{origin+low,origin+std::min(low+1,extent-1),unsigned((p-low)*256+.5f)};
    };
    std::vector<Sample> columns(v.width);
    for(unsigned x=0;x<v.width;++x)columns[x]=sample(x,v.width,a.x,a.width);
    // Separable bilinear: horizontal work is done once per source row, not
    // twice per output row. Retain fractional precision until the final pass.
    std::vector<unsigned> horizontal(std::size_t(a.height)*v.width*4);
    for(unsigned y=0;y<a.height;++y){
        const auto* row=source.rgba.data()+std::size_t(y+a.y)*source.width*4;
        auto* out=horizontal.data()+std::size_t(y)*v.width*4;
        for(unsigned x=0;x<v.width;++x){
            const auto col=columns[x];
            for(unsigned c=0;c<3;++c)out[x*4+c]=row[col.low*4+c]*(256-col.weight)+row[col.high*4+c]*col.weight;
            out[x*4+3]=255*256;
        }
    }
    for(unsigned y=0;y<v.height;++y){
        const auto row=sample(y,v.height,0,a.height);
        const auto* top=horizontal.data()+std::size_t(row.low)*v.width*4;
        const auto* bottom=horizontal.data()+std::size_t(row.high)*v.width*4;
        auto* out=destination.rgba.data()+(std::size_t(y+v.y)*destination.width+v.x)*4;
        for(unsigned i=0;i<v.width*4;++i)out[i]=static_cast<unsigned char>((top[i]*(256-row.weight)+bottom[i]*row.weight+32768)>>16);
    }
}
}
