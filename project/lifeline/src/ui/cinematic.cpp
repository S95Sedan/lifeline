#include "ui/cinematic.hpp"
#include "platform/menu_input.hpp"
#include "media/movie_playback.hpp"
#include <deque>
#include <stdexcept>
#include <sstream>
#include <atomic>
namespace lifeline {
namespace {void checked(bool ok){if(!ok)throw std::runtime_error(std::string("Cinematic SDL: ")+SDL_GetError());}}
struct Cinematic::Impl {
    SDL_Renderer* renderer;
    MoviePlayback playback;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
    unsigned current=~0u;
    static inline std::atomic<std::uint64_t> generations{std::uint64_t(1)<<63};
    std::uint64_t generation=++generations;
    Impl(const AssetDirectory& a,SDL_Renderer* r,const std::string& n,bool c,unsigned f,bool m):renderer(r),playback(a,n,c,f,m){
        const auto& picture=playback.image();
        texture.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,picture.width,picture.height));checked(bool(texture));
        checked(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));
    }
    void render(){
        if(current!=playback.current_frame()){
            const auto& picture=playback.image();checked(SDL_UpdateTexture(texture.get(),nullptr,picture.rgba.data(),picture.width*4));current=playback.current_frame();
        }
        checked(SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
        int width=0,height=0;checked(SDL_GetRenderOutputSize(renderer,&width,&height));
        const auto a=playback.aperture();const auto v=movie_viewport(a,width,height);
        const SDL_FRect source{float(a.x),float(a.y),float(a.width),float(a.height)};
        const SDL_FRect destination{float(v.x),float(v.y),float(v.width),float(v.height)};
        checked(SDL_SetRenderDrawColor(renderer,0,0,0,255));checked(SDL_RenderClear(renderer));checked(SDL_RenderTexture(renderer,texture.get(),&source,&destination));
    }
};
Cinematic::Cinematic(const AssetDirectory& a,SDL_Renderer* r,const std::string& n,bool capture,unsigned frame,bool mono):impl_(std::make_unique<Impl>(a,r,n,capture,frame,mono)){}
Cinematic::~Cinematic()=default;
bool Cinematic::advance(){
    const auto previous=impl_->playback.current_section();const bool active=impl_->playback.advance();
    if(previous!=impl_->playback.current_section())impl_->generation=++Impl::generations;
    return active;
}
bool Cinematic::skip_section(){
    if(!impl_->playback.skip_section())return false;
    impl_->generation=++Impl::generations;return true;
}
void Cinematic::render(){impl_->render();}
unsigned Cinematic::current_frame()const{return impl_->playback.current_frame();}
std::uint64_t Cinematic::playback_generation()const{return impl_->generation;}
}
