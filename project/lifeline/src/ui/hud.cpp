#include "ui/hud.hpp"
#include "ui/geometry_batch.hpp"
#include "assets/flash.hpp"
#include "assets/installed_paths.hpp"
#include "speech/microphone_spectrum.hpp"
#include <array>
#include <bit>
#include <cmath>
namespace lifeline {
namespace {
void checked(bool ok){if(!ok)throw std::runtime_error(std::string("Tutorial HUD: ")+SDL_GetError());}
struct Panel {
    FlashMovie movie;FlashPlayer player;
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    std::map<unsigned,Texture> textures;
    Panel(const AssetDirectory& assets,SDL_Renderer* renderer,const std::string& path):movie(assets.read(path),path),player(movie){
        for(const auto& [id,image]:movie.textures){Texture t(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);checked(bool(t));checked(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));checked(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));textures.emplace(id,std::move(t));}
    }
    void draw(SDL_Renderer* renderer,float x,float y)const{
        GeometryBatch geometry(renderer);for(const auto& batch:player.compose())geometry.append(batch.textured?textures.at(batch.texture).get():nullptr,batch,[&](const FlashVertex& v){return SDL_FPoint{v.x+x,v.y+y};});geometry.flush();
    }
};
}
struct GameHud::Impl {
    AssetDirectory assets;
    SDL_Renderer* renderer;std::array<std::unique_ptr<Panel>,4> mic;Panel score;
    MicrophoneDisplay state=MicrophoneDisplay::hidden;bool score_visible=false;
    std::array<float,16> bands{};MicrophoneSpectrumDisplay spectrum;
    std::optional<int> confidence;
    float offset=0;
    std::optional<float> health_target,health_display;
    Impl(const AssetDirectory& a,SDL_Renderer* r):assets(a),renderer(r),score(a,r,std::string(installed::tutorial_score)){
        for(unsigned i=0;i<4;++i)mic[i]=std::make_unique<Panel>(a,r,"ui/microphone/micg0"+std::to_string(i)+".cf");
    }
    unsigned active()const{return state==MicrophoneDisplay::cannot_talk?1:state==MicrophoneDisplay::talk_now?2:state==MicrophoneDisplay::disconnected?3:0;}
    void draw_meters(){
        checked(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND));
        constexpr int indices[]{0,1,2,2,1,3};
        constexpr float pi=std::bit_cast<float>(std::uint32_t{0x40490fd0});
        // ELF 166070: inventory record 1, quantity / 1000, 1/8 smoothing.
        // Origin is microphone panel + (30,7); dimensions 149 by 3.05.
        if(health_display){
            const float hp=std::clamp(*health_display,0.f,1.f);
            checked(SDL_SetRenderDrawColorFloat(renderer,std::clamp(2*(1-hp),0.f,1.f),std::clamp(2*hp,0.f,1.f),0,.8f));
            const SDL_FRect rect{30,11+offset,149*hp,3.05f};
            checked(SDL_RenderFillRect(renderer,&rect));
        }
        const auto& levels=spectrum.levels();
        for(unsigned i=0;i<16;++i){
            const float y=12+offset+float(i)*2.5f,fade=std::abs(std::sin(pi/16*float(i)));
            const SDL_FColor left{7.f/256,204.f/256,204.f/256,.3f*fade},right{left.r,left.g,left.b,levels[i]*fade};
            const SDL_Vertex vertices[]{
                {{30,y},left,{}},{{30+levels[i]*120,y},right,{}},
                {{30,y+2.5f},left,{}},{{30+levels[(i+1)%16]*120,y+2.5f},right,{}}};
            checked(SDL_RenderGeometry(renderer,nullptr,vertices,4,indices,6));
        }
        if(confidence && *confidence>0){
            const auto value=float(std::clamp(*confidence,0,10000))/10000;
            const float top=16+offset+(1-value)*40;
            // The CF right frame is at x=18. Match the left gutter without
            // moving x=10; SDL line endpoints include their final pixel.
            constexpr float right=17;
            const SDL_FColor bottom_color{7.f/256,204.f/256,204.f/256,.5f},top_color{bottom_color.r,bottom_color.g,bottom_color.b,1};
            const SDL_Vertex vertices[]{{{10,top},top_color,{}},{{right,top},top_color,{}},{{10,56+offset},bottom_color,{}},{{right,56+offset},bottom_color,{}}};
            checked(SDL_RenderGeometry(renderer,nullptr,vertices,4,indices,6));
            checked(SDL_SetRenderDrawColor(renderer,0,0,0,128));
            for(int line=10;line>int(std::round((1-value)*10));--line)checked(SDL_RenderLine(renderer,10,16+offset+float(line)*4,right-1,16+offset+float(line)*4));
        }
    }
};
GameHud::GameHud(const AssetDirectory& a,SDL_Renderer* r):impl_(std::make_unique<Impl>(a,r)){}
GameHud::~GameHud()=default;
void GameHud::microphone(MicrophoneDisplay state){if(state!=impl_->state){impl_->state=state;if(impl_->mic[impl_->active()])impl_->mic[impl_->active()]->player.seek(0);}}
void GameHud::microphone_assets(bool available){
    if(!available){for(auto& panel:impl_->mic)panel.reset();impl_->health_display.reset();return;}
    if(impl_->mic[0])return;
    std::array<std::unique_ptr<Panel>,4> next;
    for(unsigned i=0;i<4;++i)next[i]=std::make_unique<Panel>(impl_->assets,impl_->renderer,"ui/microphone/micg0"+std::to_string(i)+".cf");
    impl_->mic=std::move(next);
}
void GameHud::health(std::optional<int> value){
    impl_->health_target=value?std::optional<float>(float(*value)/1000):std::nullopt;
    if(!impl_->health_display||!value)impl_->health_display=impl_->health_target;
}
void GameHud::spectrum(const std::array<float,16>& decibels){impl_->bands=decibels;}
void GameHud::combat_layout(bool enabled){impl_->offset=enabled?10.f:0.f;}
void GameHud::vertical_offset(float value){impl_->offset=value;}
void GameHud::score(bool visible,std::optional<int> value){
    impl_->score_visible=visible;
    impl_->confidence=value;
    // The numeric tutorial panel and microphone meter have separate visibility.
    // Message 36000 supplies the latest result to the meter even while the
    // numeric panel is hidden (1651d8..165224, 165dc0..166064).
    impl_->score.player.set_text("text_sc",value?std::to_string(*value):"",24,24,{0,1,.7f,1});
}
void GameHud::advance(){impl_->score.player.advance();if(impl_->state!=MicrophoneDisplay::hidden&&impl_->mic[impl_->active()]){impl_->mic[impl_->active()]->player.advance();impl_->spectrum.advance(impl_->bands);if(impl_->health_target&&impl_->health_display)*impl_->health_display+=(*impl_->health_target-*impl_->health_display)*.125f;}}
void GameHud::render(){
    // ELF 0x1652dc: (-320,-220). CB 0x617c4: score (120,-224).
    if(impl_->state!=MicrophoneDisplay::hidden&&impl_->mic[impl_->active()]){impl_->mic[impl_->active()]->draw(impl_->renderer,0,4+impl_->offset);impl_->draw_meters();}
    if(impl_->score_visible)impl_->score.draw(impl_->renderer,440,0);
}
}
