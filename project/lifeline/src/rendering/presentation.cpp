#include "rendering/presentation.hpp"
#include <cmath>
namespace lifeline {
const AssetImage& Presentation::compose(const AssetImage& source,int mode,bool preserve_resolution){
    frame_scale(source);
    if(!preserve_resolution&&(source.width!=640||source.height!=448))throw std::runtime_error("Reference presentation requires a 640x448 frame");
    const float alpha=float(unsigned(mode==0||mode==1?128:mode)&255)/128.f;
    const unsigned width=preserve_resolution?source.width:512,height=preserve_resolution?source.height:448;
    if(output_.width!=width||output_.height!=height)output_={"Presented frame",width,height,Bytes(std::size_t(width)*height*4)};
    if(preserve_resolution){
        // Keep authored exposure and afterimage blending, without the original
        // strip-copy resampling and its 512-pixel horizontal bottleneck.
        for(std::size_t i=0;i<source.rgba.size();i+=4){
            for(unsigned k=0;k<3;++k){
                const float color=float(std::min(unsigned(source.rgba[i+k])*153/128,255u));
                output_.rgba[i+k]=static_cast<unsigned char>(std::clamp(std::floor((color-output_.rgba[i+k])*alpha)+output_.rgba[i+k],0.f,255.f));
            }
            output_.rgba[i+3]=255;
        }
        return output_;
    }
    const bool linear=mode!=1;
    const auto sample=[&](float u,float v,unsigned k){
        if(!linear){const auto x=unsigned(std::clamp(std::floor(u),0.f,639.f)),y=unsigned(std::clamp(std::floor(v),0.f,447.f));return float(source.rgba[(y*640+x)*4+k]);}
        const float x=std::clamp(u-.5f,0.f,639.f),y=std::clamp(v-.5f,0.f,447.f);const unsigned ix=unsigned(x),iy=unsigned(y),jx=std::min(ix+1,639u),jy=std::min(iy+1,447u);const float ax=x-ix,ay=y-iy;
        return (source.rgba[(iy*640+ix)*4+k]*(1-ax)+source.rgba[(iy*640+jx)*4+k]*ax)*(1-ay)+(source.rgba[(jy*640+ix)*4+k]*(1-ax)+source.rgba[(jy*640+jx)*4+k]*ax)*ay;
    };
    // 1ce590 emits ten columns and fourteen rows. The later stores at
    // 1ce6d8..1ce708 overwrite the first rectangle's UV-Y/XY-X fields;
    // the second rectangle's coordinates remain zero (degenerate).
    for(unsigned column=0;column<10;++column){
        const float left=float(column*512/10)+1,right=float((column+1)*512/10)+.5f;
        for(unsigned row=0;row<14;++row)for(unsigned y=row*32;y<(row+1)*32;++y)for(unsigned x=unsigned(left);x<512&&float(x)<right;++x){
            const float u=column*64+.5f+(float(x)-left)/(right-left)*63.5f;
            const float v=float(y)+1;
            auto* out=output_.rgba.data()+(y*512+x)*4;
            for(unsigned k=0;k<3;++k){
                const float color=std::clamp(std::floor(sample(u,v,k)*153.f/128.f),0.f,255.f);
                out[k]=static_cast<unsigned char>(std::clamp(std::floor((color-out[k])*alpha)+out[k],0.f,255.f));
            }
            out[3]=255;
        }
    }
    // Display RGB is opaque, including the untouched overscan column.
    for(unsigned y=0;y<448;++y)output_.rgba[(y*512)*4+3]=255;
    return output_;
}
}
