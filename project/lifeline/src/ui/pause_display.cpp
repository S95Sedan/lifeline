#include "ui/pause_display.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <vector>
namespace lifeline {
namespace {void check(bool b){if(!b)throw std::runtime_error(SDL_GetError());}}
struct PauseDisplay::Impl {
    SDL_Renderer* renderer;
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
    std::vector<Texture> textures;
    Texture settings{nullptr,SDL_DestroyTexture};
    Texture texture(SDL_Surface* image){
        Texture result(SDL_CreateTextureFromSurface(renderer,image),SDL_DestroyTexture);check(bool(result));
        check(SDL_SetTextureBlendMode(result.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(result.get(),SDL_SCALEMODE_LINEAR));return result;
    }
    void settings_row(SDL_Surface* original){
        // Reuse the authored Continue frame and glyphs at their original size,
        // baseline and spacing. This also preserves their teal and dark shadow.
        Surface row(SDL_CreateSurface(250,21,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);check(bool(row));
        check(SDL_SetSurfaceBlendMode(original,SDL_BLENDMODE_NONE));
        SDL_Rect frame{37,36,250,21};check(SDL_BlitSurface(original,&frame,row.get(),nullptr));
        SDL_Rect caption{73,3,104,16};check(SDL_FillSurfaceRect(row.get(),&caption,0));

        // The pause captions contain E, T, I, N and G, but no S. Extend their
        // 11x15, two-pixel stroke alphabet with S and the same one-pixel shadow.
        constexpr std::array<const char*,15> s{
            "  #######  "," ######### ","###     ###","##       ##",
            "##         ","###        "," ########  ","  ######## ",
            "        ###","         ##","         ##","##       ##",
            "###     ###"," ######### ","  #######  "};
        Surface letter(SDL_CreateSurface(12,16,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);check(bool(letter));
        check(SDL_FillSurfaceRect(letter.get(),nullptr,0));check(SDL_SetSurfaceBlendMode(letter.get(),SDL_BLENDMODE_NONE));
        const auto pixel=[](SDL_Surface* image,int x,int y){return static_cast<unsigned char*>(image->pixels)+y*image->pitch+x*4;};
        for(int y=0;y<15;++y)for(int x=0;x<11;++x)if(s[y][x]=='#')std::copy_n(pixel(original,213,40),4,pixel(letter.get(),x+1,y+1));
        for(int y=0;y<15;++y)for(int x=0;x<11;++x)if(s[y][x]=='#')std::copy_n(pixel(original,202,39),4,pixel(letter.get(),x,y));

        constexpr std::array<SDL_Rect,8> glyphs{{
            {0,0,12,16},{202,39,12,16},{152,39,12,16},{152,39,12,16},
            {166,39,6,16},{138,39,12,16},{72,77,12,16},{0,0,12,16}}};
        int x=73;
        for(unsigned i=0;i<glyphs.size();++i){
            SDL_Rect destination{x,3,0,0};
            check(SDL_BlitSurface(i==0 || i==7?letter.get():original,&glyphs[i],row.get(),&destination));
            x+=glyphs[i].w+2;
        }
        settings=texture(row.get());
    }
    Impl(const AssetDirectory& assets,SDL_Renderer* r):renderer(r){
        for(unsigned i=1;i<=4;++i){
            const auto bytes=assets.read("ui/pause/pause0"+std::to_string(i)+".bmp");
            auto* io=SDL_IOFromConstMem(bytes.data(),bytes.size());check(bool(io));
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> raw(SDL_LoadBMP_IO(io,true),SDL_DestroySurface);check(bool(raw));
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);check(bool(image));
            for(int y=0;y<image->h;++y)for(int x=0;x<image->w;++x){auto* pixel=static_cast<unsigned char*>(image->pixels)+y*image->pitch+x*4;pixel[3]=static_cast<unsigned char>(std::min(255u,unsigned(pixel[3])*2));}
            if(i==1)settings_row(image.get());
            textures.push_back(texture(image.get()));
        }
    }
    void sprite(unsigned image,SDL_FRect source,SDL_FRect destination,float alpha=1){check(SDL_SetTextureAlphaModFloat(textures[image].get(),std::clamp(alpha,0.f,1.f)));check(SDL_RenderTexture(renderer,textures[image].get(),&source,&destination));}
};
PauseDisplay::PauseDisplay(const AssetDirectory& a,SDL_Renderer* r):impl_(std::make_unique<Impl>(a,r)){}
PauseDisplay::~PauseDisplay()=default;
void PauseDisplay::render(const Pause& p){
    if(!p.active())return;auto& d=*impl_;
    int width=0,height=0;SDL_RendererLogicalPresentation mode;
    check(SDL_GetRenderLogicalPresentation(d.renderer,&width,&height,&mode));
    // Presentation leaves the renderer in output-pixel coordinates. Dim the
    // complete output, then draw authored sprites in the centered HUD space.
    check(SDL_SetRenderLogicalPresentation(d.renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
    check(SDL_SetRenderDrawBlendMode(d.renderer,SDL_BLENDMODE_BLEND));check(SDL_SetRenderDrawColor(d.renderer,0,0,0,128));check(SDL_RenderFillRect(d.renderer,nullptr));
    check(SDL_SetRenderLogicalPresentation(d.renderer,640,448,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    d.sprite(0,{0,0,320,63},{160,104,320,63});
    d.sprite(0,{0,63,320,33},{160,205,320,33});
    SDL_FRect settings{197,178,250,21};check(SDL_RenderTexture(d.renderer,d.settings.get(),nullptr,&settings));
    if(p.state()==1)d.sprite(1,{0,0,384,128},{128,244,384,128});
    d.sprite(2,{0,0,256,16},{192,130+38.f*p.selection(),256,32},p.state()==1?1:p.pulse());
    if(p.state()==1)d.sprite(3,{0,0,160,32},{156+168.f*p.confirmation(),342,160,32},p.pulse());
    check(SDL_SetRenderLogicalPresentation(d.renderer,width,height,mode));
}
}
