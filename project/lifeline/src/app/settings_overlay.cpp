#include "app/settings_overlay.hpp"
#include "app/config.hpp"
#include "assets/installed_paths.hpp"
#include "ui/menus.hpp"
#include "ui/geometry_batch.hpp"
#include "platform/menu_input.hpp"
#include "platform/microphone_monitor.hpp"
#include "rendering/renderer.hpp"
#include "media/audio_output.hpp"
#include "media/audio_preview.hpp"
#include <algorithm>
#include <map>

namespace lifeline {
namespace {
void checked(bool ok){if(!ok)throw std::runtime_error(std::string("In-game settings: ")+SDL_GetError());}
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
struct OutputCoordinates {
    SDL_Renderer* renderer;int width=0,height=0;SDL_RendererLogicalPresentation mode{};
    explicit OutputCoordinates(SDL_Renderer* r):renderer(r){checked(SDL_GetRenderLogicalPresentation(r,&width,&height,&mode));checked(SDL_SetRenderLogicalPresentation(r,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));}
    ~OutputCoordinates(){SDL_SetRenderLogicalPresentation(renderer,width,height,mode);}
};
}
struct SettingsOverlay::Impl {
    SDL_Renderer* renderer;
    OptionsAssets assets;
    MenuSettings settings,persisted;
    std::filesystem::path settings_path;
    std::unique_ptr<OptionsController> menu;
    std::map<unsigned,Texture> textures;
    MenuInput input;
    AppConfig applied;
    AudioPreview preview;
    MicrophoneMonitor microphone;
    bool focused=true;
    Impl(const AssetDirectory& data,SDL_Renderer* r):renderer(r),assets(data.read(installed::program("entry_1017.cb"))),settings_path(user_config_path().parent_path()/"menu-settings.txt"),applied(app_config()),preview(data){
        for(const auto* name:{"op_title.cf","op_keya.cf","op_keyb.cf","op_keyc.cf","op_sound.cf","op_screen.cf","op_bright.cf","op_yesno.cf"})assets.movies.emplace(name,FlashMovie(data.read(std::string("ui/options/")+name),name));
        settings=persisted=read_menu_settings(settings_path);input_bindings()=settings.bindings;input.fixed_navigation(true);
        auto* window=SDL_GetRenderWindow(renderer);menu=std::make_unique<OptionsController>(assets,settings,app_config(),window?SDL_GetDisplayForWindow(window):SDL_GetPrimaryDisplay(),OptionsController::Layout::side_panel);
        for(const auto& [id,image]:menu->root().movie().textures){Texture texture(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);checked(bool(texture));checked(SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),int(image.width*4)));checked(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND));checked(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));textures.emplace(id,std::move(texture));}
    }
};
SettingsOverlay::SettingsOverlay(const AssetDirectory& data,SDL_Renderer* r):impl_(std::make_unique<Impl>(data,r)){}
SettingsOverlay::~SettingsOverlay()=default;
SDL_FRect SettingsOverlay::rectangle(SDL_Renderer* renderer){
    int width=0,height=0;checked(SDL_GetRenderOutputSize(renderer,&width,&height));
    const float margin=std::min(width,height)*.02f,scale=std::min((height-2*margin)/448.f,width*.42f/360.f);
    return {width-360*scale-margin,(height-448*scale)*.5f,360*scale,448*scale};
}
void SettingsOverlay::event(const SDL_Event& event){
    auto& d=*impl_;
    audio_device_event(event);
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){d.focused=false;d.preview.stop();d.microphone.stop();d.menu->take_audio_preview();}
    if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)d.focused=true;
    if(d.menu->event(event,d.input.gamepad_id())){d.input.reset();return;}
    if(event.type==SDL_EVENT_MOUSE_MOTION||event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){
        OutputCoordinates coordinates(d.renderer);SDL_Event converted=event;checked(SDL_ConvertEventToRenderCoordinates(d.renderer,&converted));const auto rect=rectangle(d.renderer);const auto scale=rect.w/360;
        const float x=event.type==SDL_EVENT_MOUSE_MOTION?converted.motion.x:converted.button.x,y=event.type==SDL_EVENT_MOUSE_MOTION?converted.motion.y:converted.button.y;
        d.menu->pointer((x-rect.x)/scale,(y-rect.y)/scale,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT);
    }
    d.input.event(event,SDL_GetTicks());
}
void SettingsOverlay::update(){
    auto& d=*impl_;
    for(const auto action:d.input.take(SDL_GetTicks())){
        if(action==MenuAction::confirm)d.menu->confirm();
        else if(action==MenuAction::back||action==MenuAction::escape)d.menu->back();
        else d.menu->move(action==MenuAction::left?-1:action==MenuAction::right?1:0,action==MenuAction::up?-1:action==MenuAction::down?1:0);
    }
    auto desired=d.menu->graphics_preview();
    try{set_audio_device_settings(desired.devices);}catch(const std::exception& error){d.menu->audio_devices_failed(audio_device_settings(),error.what());desired=d.menu->graphics_preview();}
    if(auto error=take_audio_device_error();!error.empty())d.menu->audio_devices_failed(audio_device_settings(),error);
    set_audio_volumes(desired.audio);
    if(desired!=d.applied){
        auto* window=SDL_GetRenderWindow(d.renderer);
        try{
            if(desired.mode!=d.applied.mode||desired.width!=d.applied.width||desired.height!=d.applied.height){if(!window)throw std::runtime_error("No display window available");apply_display_settings(window,desired);}
            if(desired.vsync!=d.applied.vsync)checked(SDL_SetRenderVSync(d.renderer,desired.vsync?1:0));
            set_scene_quality(desired.render_scale,desired.texture_filter,desired.quality);d.applied=desired;
        }catch(const std::exception& error){if(window)apply_display_settings(window,app_config());SDL_SetRenderVSync(d.renderer,app_config().vsync?1:0);set_scene_quality(app_config().render_scale,app_config().texture_filter,app_config().quality);d.applied=app_config();d.applied.audio=desired.audio;d.applied.devices=desired.devices;d.menu->display_failed(error.what());}
    }
    // Commit before closing, including a change and two Escapes in one batch.
    d.menu->graphics_applied();
    if(d.menu->graphics()!=app_config()){app_config()=d.menu->graphics();write_app_config(user_config_path(),app_config());}
    if(d.settings!=d.persisted){mono_output=d.settings.sound==1;write_menu_settings(d.settings_path,d.settings);d.persisted=d.settings;}
    if(d.focused&&d.menu->page()==OptionsController::Page::sound){if(auto bus=d.menu->take_audio_preview())d.preview.play(*bus);d.preview.update();if(auto error=d.preview.take_error();!error.empty())d.menu->audio_preview_failed(error);}
    else {d.menu->take_audio_preview();d.preview.stop();}
    d.microphone.update(d.focused&&d.menu->page()==OptionsController::Page::microphone);
    d.menu->microphone_meter(d.microphone.level(),d.microphone.clipping(),d.microphone.listening(),d.microphone.error());
}
void SettingsOverlay::render(){
    auto& d=*impl_;OutputCoordinates coordinates(d.renderer);const auto rect=rectangle(d.renderer);const auto scale=rect.w/360;
    checked(SDL_SetRenderDrawBlendMode(d.renderer,SDL_BLENDMODE_BLEND));
    GeometryBatch geometry(d.renderer);for(const auto& batch:d.menu->root().compose())geometry.append(batch.textured?d.textures.at(batch.texture).get():nullptr,batch,[&](const FlashVertex& v){return SDL_FPoint{rect.x+v.x*scale,rect.y+v.y*scale};});geometry.flush();
}
bool SettingsOverlay::closed()const{return impl_->menu->page()==OptionsController::Page::closed;}
const OptionsController& SettingsOverlay::controller()const{return *impl_->menu;}
}
