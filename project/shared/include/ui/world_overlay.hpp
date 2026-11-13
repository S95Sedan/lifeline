#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>

namespace lifeline {
// Native camera coordinates remain centered on (320,224), even outside the
// original HUD rectangle. Map them to the full output without HUD clipping.
class WorldOverlayViewport {
    SDL_Renderer* renderer_;
    int old_width_=0,old_height_=0;
    SDL_RendererLogicalPresentation old_mode_{};
    static void check(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}
public:
    float scale=1,x=0,y=0;
    explicit WorldOverlayViewport(SDL_Renderer* renderer):renderer_(renderer){
        check(SDL_GetRenderLogicalPresentation(renderer,&old_width_,&old_height_,&old_mode_));
        int width=0,height=0;
        if(auto* target=SDL_GetRenderTarget(renderer)){float w,h;check(SDL_GetTextureSize(target,&w,&h));width=int(w);height=int(h);}
        else check(SDL_GetRenderOutputSize(renderer,&width,&height));
        scale=std::min(width/640.f,height/448.f);x=(width-640*scale)*.5f;y=(height-448*scale)*.5f;
        check(SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
    }
    ~WorldOverlayViewport(){SDL_SetRenderLogicalPresentation(renderer_,old_width_,old_height_,old_mode_);}
    SDL_FPoint point(float px,float py)const{return {px*scale+x,py*scale+y};}
};
}
