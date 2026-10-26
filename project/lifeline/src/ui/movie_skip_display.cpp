#include "ui/movie_skip_display.hpp"
#include "ui/input_prompts.hpp"
#include "ui/world_overlay.hpp"
#include "ui/geometry_batch.hpp"
#include <SDL3/SDL.h>
namespace lifeline {
namespace {void check(bool ok){if(!ok)throw std::runtime_error(std::string("Movie skip display: ")+SDL_GetError());}}
struct MovieSkipDisplay::Impl {
    SDL_Renderer* renderer;
    FlashMovie font;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
    Impl(const AssetDirectory& assets,SDL_Renderer* r):renderer(r),font(assets.read("ui/scenes/tutorial/msg.cf"),"msg.cf"){
        const auto& atlas=font.textures.at(font.texts.at(5).font);texture.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,atlas.width,atlas.height));check(bool(texture));check(SDL_UpdateTexture(texture.get(),nullptr,atlas.rgba.data(),atlas.width*4));check(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));
    }
    void render(float progress){
        WorldOverlayViewport viewport(renderer);int width=0,height=0;check(SDL_GetCurrentRenderOutputSize(renderer,&width,&height));const float scale=viewport.scale;
        const float right=width/scale-16,bottom=height/scale-16,left=right-240;
        auto label=movie_skip_prompt(font,{left,bottom-37,right,bottom-9});auto shadow=label;
        for(auto& v:shadow.vertices){v.x+=1;v.y+=1;v.color={0,0,0,.85f};}
        GeometryBatch geometry(renderer);const auto point=[&](const FlashVertex& v){return SDL_FPoint{v.x*scale,v.y*scale};};geometry.append(texture.get(),shadow,point);geometry.append(texture.get(),label,point);geometry.flush();
        Uint8 r,g,b,a;SDL_BlendMode blend;check(SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a));check(SDL_GetRenderDrawBlendMode(renderer,&blend));check(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND));
        const SDL_FRect bar{left*scale,(bottom-5)*scale,240*scale,5*scale};check(SDL_SetRenderDrawColor(renderer,0,0,0,180));check(SDL_RenderFillRect(renderer,&bar));
        const SDL_FRect fill{bar.x+scale,bar.y+scale,(bar.w-2*scale)*std::clamp(progress,0.f,1.f),bar.h-2*scale};check(SDL_SetRenderDrawColor(renderer,0,230,200,254));check(SDL_RenderFillRect(renderer,&fill));check(SDL_RenderRect(renderer,&bar));
        check(SDL_SetRenderDrawColor(renderer,r,g,b,a));check(SDL_SetRenderDrawBlendMode(renderer,blend));
    }
};
MovieSkipDisplay::MovieSkipDisplay(const AssetDirectory& a,SDL_Renderer* r):impl_(std::make_unique<Impl>(a,r)){}
MovieSkipDisplay::~MovieSkipDisplay()=default;
void MovieSkipDisplay::render(float progress){impl_->render(progress);}
}
