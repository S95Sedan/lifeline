#include "ui/presentation_display.hpp"
#include "rendering/presentation.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>
#include <cmath>
namespace lifeline {
namespace {void check(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}}
struct PresentationDisplay::Impl {
    SDL_Renderer* renderer;SDL_Texture* previous=nullptr;
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    Texture capture{nullptr,SDL_DestroyTexture},exposed{nullptr,SDL_DestroyTexture},upload{nullptr,SDL_DestroyTexture};
    std::array<Texture,2> history{Texture{nullptr,SDL_DestroyTexture},Texture{nullptr,SDL_DestroyTexture}};
    Presentation presentation;bool capturing=false,native;float scale;unsigned width,height;
    unsigned current=0;bool history_valid=false;
    explicit Impl(SDL_Renderer* r,float s,bool preserve,unsigned w=0,unsigned h=0):renderer(r),native(preserve||s!=1),scale(s),width(w?w:unsigned(std::lround(640*s))),height(h?h:unsigned(std::lround(448*s))){
        if(!std::isfinite(s)||s<=0||s>12)throw std::runtime_error("Invalid presentation scale");
        capture.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,int(width),int(height)));check(bool(capture));
        const unsigned output_width=native?width:512;
        upload.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,int(output_width),int(height)));check(bool(upload));
        check(SDL_SetTextureBlendMode(upload.get(),SDL_BLENDMODE_NONE));check(SDL_SetTextureScaleMode(upload.get(),SDL_SCALEMODE_LINEAR));
        if(native){exposed.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,int(width),int(height)));check(bool(exposed));
            for(auto& texture:history){texture.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,int(width),int(height)));check(bool(texture));check(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_NONE));check(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));}}
    }
    void present(SDL_Texture* texture,int offset_x,int offset_y){
        check(SDL_SetRenderLogicalPresentation(renderer,native?int(width):640,native?int(height):448,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        check(SDL_SetRenderDrawColor(renderer,0,0,0,255));check(SDL_RenderClear(renderer));
        const SDL_FRect destination{float(offset_x)*(native?scale:1),float(offset_y)*(native?scale:1),native?float(width):640,native?float(height):448};
        check(SDL_RenderTexture(renderer,texture,nullptr,&destination));
    }
    ~Impl(){if(capturing)SDL_SetRenderTarget(renderer,previous);}
};
PresentationDisplay::PresentationDisplay(SDL_Renderer* renderer,float scale,bool preserve):impl_(std::make_unique<Impl>(renderer,scale,preserve)){}
PresentationDisplay::PresentationDisplay(SDL_Renderer* renderer,unsigned width,unsigned height)
    :impl_(std::make_unique<Impl>(renderer,std::min(float(width)/640,float(height)/448),true,width,height)){}
PresentationDisplay::~PresentationDisplay()=default;
void PresentationDisplay::begin(){
    auto& p=*impl_;if(p.capturing)throw std::runtime_error("Presentation capture already active");p.previous=SDL_GetRenderTarget(p.renderer);
    check(SDL_SetRenderTarget(p.renderer,p.capture.get()));p.capturing=true;
    check(SDL_SetRenderLogicalPresentation(p.renderer,640,448,p.native?SDL_LOGICAL_PRESENTATION_LETTERBOX:SDL_LOGICAL_PRESENTATION_DISABLED));
}
void PresentationDisplay::finish(int mode,int offset_x,int offset_y){
    auto& p=*impl_;if(!p.capturing)throw std::runtime_error("Presentation capture is not active");
    // High-resolution hardware presentation normally uses alpha <= 1. Keep
    // both the composed frame and its history GPU-resident instead of reading
    // 4.4 MiB back to the CPU and uploading it again every frame.
    const float alpha=float(unsigned(mode==0||mode==1?128:mode)&255)/128.f;
    if(p.native&&alpha<=1){
        const unsigned next=p.current^1;const bool replace=alpha==1&&p.history_valid;
        check(SDL_SetRenderTarget(p.renderer,replace?p.history[next].get():p.exposed.get()));
        check(SDL_SetRenderLogicalPresentation(p.renderer,int(p.width),int(p.height),SDL_LOGICAL_PRESENTATION_DISABLED));
        check(SDL_SetTextureBlendMode(p.capture.get(),SDL_BLENDMODE_NONE));check(SDL_RenderTexture(p.renderer,p.capture.get(),nullptr,nullptr));
        check(SDL_SetTextureAlphaModFloat(p.capture.get(),25.f/128.f));check(SDL_SetTextureBlendMode(p.capture.get(),SDL_BLENDMODE_ADD));check(SDL_RenderTexture(p.renderer,p.capture.get(),nullptr,nullptr));check(SDL_SetTextureAlphaModFloat(p.capture.get(),1));
        if(p.history_valid)check(SDL_SetRenderColorScale(p.renderer,1));
        if(!replace){
            check(SDL_SetRenderTarget(p.renderer,p.history[next].get()));
            if(alpha<1){
                if(p.history_valid){check(SDL_SetTextureBlendMode(p.history[p.current].get(),SDL_BLENDMODE_NONE));check(SDL_RenderTexture(p.renderer,p.history[p.current].get(),nullptr,nullptr));}
                else {check(SDL_SetRenderDrawColor(p.renderer,0,0,0,255));check(SDL_RenderClear(p.renderer));}
            }
            const float gpu_alpha=alpha==1?1:(float((unsigned(mode)&255)*2+1)/256.f);
            check(SDL_SetTextureAlphaModFloat(p.exposed.get(),gpu_alpha));check(SDL_SetTextureBlendMode(p.exposed.get(),alpha==1?SDL_BLENDMODE_NONE:SDL_BLENDMODE_BLEND));check(SDL_RenderTexture(p.renderer,p.exposed.get(),nullptr,nullptr));check(SDL_SetTextureAlphaModFloat(p.exposed.get(),1));
        }
        p.current=next;p.history_valid=true;check(SDL_SetRenderTarget(p.renderer,p.previous));p.capturing=false;
        p.present(p.history[p.current].get(),offset_x,offset_y);return;
    }
    // Values above 128 extrapolate rather than alpha-blend. They are rare and
    // retain the exact recovered CPU arithmetic. Seed that arithmetic from the
    // current GPU history before crossing the boundary.
    if(p.native&&p.history_valid){
        check(SDL_SetRenderTarget(p.renderer,p.history[p.current].get()));
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> prior(SDL_RenderReadPixels(p.renderer,nullptr),SDL_DestroySurface);check(bool(prior));
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> rgba(SDL_ConvertSurface(prior.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);check(bool(rgba));
        AssetImage seed{"Presented frame",p.width,p.height,Bytes(std::size_t(p.width)*p.height*4)};
        for(unsigned y=0;y<p.height;++y)std::memcpy(seed.rgba.data()+y*p.width*4,static_cast<const unsigned char*>(rgba->pixels)+y*rgba->pitch,p.width*4);
        p.presentation.seed(seed);check(SDL_SetRenderTarget(p.renderer,p.capture.get()));
    }
    // Read the complete framebuffer, not the centered logical HUD viewport.
    check(SDL_SetRenderLogicalPresentation(p.renderer,int(p.width),int(p.height),SDL_LOGICAL_PRESENTATION_DISABLED));
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> read(SDL_RenderReadPixels(p.renderer,nullptr),SDL_DestroySurface);check(bool(read));
    std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_ConvertSurface(read.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);check(bool(pixels));
    AssetImage image{"Composed scene and interface",p.width,p.height,Bytes(std::size_t(p.width)*p.height*4)};
    if(pixels->w!=int(p.width)||pixels->h!=int(p.height))throw std::runtime_error("Presentation capture dimensions changed");
    for(unsigned y=0;y<p.height;++y)std::memcpy(image.rgba.data()+y*p.width*4,static_cast<const unsigned char*>(pixels->pixels)+y*pixels->pitch,p.width*4);
    const auto& output=p.presentation.compose(image,mode,p.native);check(SDL_UpdateTexture(p.upload.get(),nullptr,output.rgba.data(),int(output.width*4)));
    if(p.native){const unsigned next=p.current^1;check(SDL_SetRenderTarget(p.renderer,p.history[next].get()));check(SDL_SetRenderLogicalPresentation(p.renderer,int(p.width),int(p.height),SDL_LOGICAL_PRESENTATION_DISABLED));check(SDL_SetTextureBlendMode(p.upload.get(),SDL_BLENDMODE_NONE));check(SDL_RenderTexture(p.renderer,p.upload.get(),nullptr,nullptr));p.current=next;p.history_valid=true;}
    check(SDL_SetRenderTarget(p.renderer,p.previous));p.capturing=false;
    p.present(p.native?p.history[p.current].get():p.upload.get(),offset_x,offset_y);
}
}
