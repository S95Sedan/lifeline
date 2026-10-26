#pragma once
#include <memory>
struct SDL_Renderer;
namespace lifeline {
class PresentationDisplay {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit PresentationDisplay(SDL_Renderer*,float scale=1,bool preserve_resolution=false);
    PresentationDisplay(SDL_Renderer*,unsigned width,unsigned height);
    ~PresentationDisplay();
    void begin();
    void finish(int mode,int offset_x=0,int offset_y=0);
};
}
