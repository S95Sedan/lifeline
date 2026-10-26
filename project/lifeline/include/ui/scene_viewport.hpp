#pragma once
#include <algorithm>

namespace lifeline {
struct SceneViewport {
    struct Rect { float x,y,w,h; } source,destination;
};
// The item/keyword panels retain their authored 640x448 layout. Restore
// their scene inset by cropping the expanded frustum, never squeezing it.
inline SceneViewport menu_scene_viewport(unsigned width,unsigned height,float scene_scale){
    const float w=float(width),h=float(height);
    const float scale=std::min(w/640.f,h/448.f);
    const float progress=std::clamp((1.f-scene_scale)/.3f,0.f,1.f);
    const float x=(w-640.f*scale)*.5f*progress;
    const float y=(h-448.f*scale)*.5f*progress;
    const float sw=w-2*x,sh=h-2*y;
    return {{x,y,sw,sh},{x+sw*(1-scene_scale),y,sw*scene_scale,sh*scene_scale}};
}
}
