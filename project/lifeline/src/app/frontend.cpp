#include "app/frontend.hpp"
#include "ui/world_overlay.hpp"
#include "app/config.hpp"
#include "ui/geometry_batch.hpp"
#include "platform/movie_skip.hpp"
#include "ui/movie_skip_display.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

namespace lifeline {
static void check_asset_window(bool ok) {
    if (!ok) throw std::runtime_error(std::string("SDL: ") + SDL_GetError());
}
void show_assets(const std::vector<AssetImage>& images, const std::string& capture, unsigned initial) {
    if (images.empty() || initial >= images.size()) throw std::runtime_error("Invalid preview image selection");
    check_asset_window(SDL_Init(SDL_INIT_VIDEO));
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
    Window window(create_game_window("Lifeline - native asset preview", !capture.empty()), SDL_DestroyWindow);
    check_asset_window(window != nullptr);
    Renderer renderer(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    check_asset_window(renderer != nullptr);
    std::cout << "Renderer: " << SDL_GetRendererName(renderer.get()) << '\n';
    SDL_SetRenderVSync(renderer.get(), 1); // Lack of vsync is not a failure.
    unsigned selected = initial;
    Texture texture(nullptr, SDL_DestroyTexture);
    auto select = [&] {
        const auto& asset = images[selected];
        texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
            static_cast<int>(asset.width), static_cast<int>(asset.height)));
        check_asset_window(texture != nullptr);
        check_asset_window(SDL_UpdateTexture(texture.get(), nullptr, asset.rgba.data(), static_cast<int>(asset.width * 4)));
        check_asset_window(SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND));
        check_asset_window(SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST));
        const auto title = "Lifeline - " + asset.name + " (" + std::to_string(selected + 1) + "/" +
            std::to_string(images.size()) + ") | Left/Right: change | Esc: close | Native asset preview";
        check_asset_window(SDL_SetWindowTitle(window.get(), title.c_str()));
    };
    select();
    bool running = true;
    unsigned rendered_frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            audio_device_event(event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_SPACE) {
                    selected = (selected + 1) % static_cast<unsigned>(images.size()); select();
                }
                if (event.key.key == SDLK_LEFT) {
                    selected = (selected + static_cast<unsigned>(images.size()) - 1) % static_cast<unsigned>(images.size()); select();
                }
            }
        }
        if (!running) break;
        int width = 0, height = 0;
        check_asset_window(SDL_GetRenderOutputSize(renderer.get(), &width, &height));
        const auto& asset = images[selected];
        const float scale = std::min(static_cast<float>(width) / static_cast<float>(asset.width),
                                     static_cast<float>(height) / static_cast<float>(asset.height));
        const SDL_FRect destination{(static_cast<float>(width) - static_cast<float>(asset.width) * scale) / 2,
            (static_cast<float>(height) - static_cast<float>(asset.height) * scale) / 2,
            static_cast<float>(asset.width) * scale, static_cast<float>(asset.height) * scale};
        check_asset_window(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
        check_asset_window(SDL_RenderClear(renderer.get()));
        check_asset_window(SDL_RenderTexture(renderer.get(), texture.get(), nullptr, &destination));
        // Give window/renderer resize events two presents to settle before readback.
        if (!capture.empty() && rendered_frames >= 2) {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
                SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
            check_asset_window(surface != nullptr);
            check_asset_window(SDL_SaveBMP(surface.get(), capture.c_str()));
            std::cout << "Saved renderer capture: " << capture << " (" << asset.name << ")\n";
            running = false;
        }
        check_asset_window(SDL_RenderPresent(renderer.get()));
        ++rendered_frames;
        SDL_Delay(8);
    }
}
}


#include "app/frontend.hpp"
#include "platform/menu_input.hpp"
#include "ui/cinematic.hpp"
#include "app/session_view.hpp"
#include "rendering/renderer.hpp"
#include "speech/backend.hpp"
#include "media/audio_output.hpp"
#include "media/audio_preview.hpp"
#include "platform/microphone_monitor.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace lifeline {
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error(std::string("SDL: ")+SDL_GetError()); }
std::filesystem::path utf8_path(const std::string& text) {return std::filesystem::path(std::u8string(text.begin(),text.end()));}
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
std::string game_window_title(){
    const auto rendering=renderer_backend()==RendererBackend::d3d12?"Hardware (D3D12)":"Software";
    return std::string("LifeLine - ")+rendering+" - "+speech_backend_name(speech_backend_configuration().backend)+" Speech";
}
struct FrameWindowTitle {
    std::string base,current;
    Uint64 sampled=SDL_GetTicksNS();
    unsigned frames=0;
    explicit FrameWindowTitle(std::string value):base(std::move(value)),current(base+" - 0 FPS"){}
    const char* text()const{return current.c_str();}
    void advance(SDL_Window* window){
        ++frames;const auto now=SDL_GetTicksNS(),elapsed=now-sampled;
        if(elapsed<500000000)return;
        const auto fps=unsigned((std::uint64_t(frames)*1000000000ull+elapsed/2)/elapsed);
        current=base+" - "+std::to_string(fps)+" FPS";frames=0;sampled=now;
        check(SDL_SetWindowTitle(window,current.c_str()));
    }
};
}
void show_movies(const std::vector<FlashMovie>& movies,const std::string& capture,unsigned frame,const FlashMovie* cursor_movie,const TitleNavigation* navigation,const OptionsAssets* option_assets,const SaveMenuAssets* save_assets,const BonusAssets* bonus_assets,const AssetDirectory* data) {
    if (movies.empty()) throw std::runtime_error("No native movies supplied");
    check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    struct Quit { ~Quit(){SDL_Quit();} } quit;
    FrameWindowTitle window_title(game_window_title());
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        create_game_window(window_title.text(),!capture.empty()),SDL_DestroyWindow);
    check(window!=nullptr);
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);
    check(renderer!=nullptr); SDL_SetRenderVSync(renderer.get(),capture.empty()&&app_config().vsync?1:0);
    check(SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_BLEND));
    std::map<unsigned,Texture> textures,cursor_textures;
    std::unique_ptr<TitleController> title;
    std::unique_ptr<OptionsController> options;
    MicrophoneMonitor microphone_monitor;
    std::unique_ptr<EmptySaveController> saves;
    std::unique_ptr<BonusController> bonus;
    std::unique_ptr<Cinematic> cinematic;
    auto movie_skip=std::make_shared<MovieSkipHold>();
    std::unique_ptr<MovieSkipDisplay> movie_skip_display;
    std::unique_ptr<SessionView> session;
    std::unique_ptr<SessionView> options_session;
    std::unique_ptr<AudioPreview> audio_preview;
    bool audio_focused=true;
    if(data&&capture.empty())audio_preview=std::make_unique<AudioPreview>(*data);
    unsigned attract_fade=0;
    MenuSettings settings;
    std::filesystem::path settings_path;
    if(capture.empty() && option_assets) {
        if(const auto* directory=SDL_getenv("LIFELINE_USER_DATA")) settings_path=utf8_path(directory);
        else settings_path=user_config_path().parent_path();
        settings_path/="menu-settings.txt";settings=read_menu_settings(settings_path);
    }
    auto persisted_settings=settings;
    std::map<std::string,std::map<unsigned,Texture>> option_textures;
    unsigned selected=0,presents=0;std::unique_ptr<FlashPlayer> player;
    const auto upload=[&](const FlashMovie& source,std::map<unsigned,Texture>& destination) {
        for (const auto& [id,image]:source.textures) {
            if(destination.contains(id))continue;
            Texture texture(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);
            check(texture!=nullptr);check(SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),int(image.width*4)));
            check(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND));
            check(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));
            destination.emplace(id,std::move(texture));
        }
    };
    auto select=[&] {
        options_session.reset();session.reset();bonus.reset();saves.reset();options.reset();title.reset();textures.clear();cursor_textures.clear();const auto& movie=movies[selected];
        player=std::make_unique<FlashPlayer>(movie);player->seek(bonus_assets && movie.name=="bg00.cf"?std::min(frame,unsigned(movie.frames.size()-1)):frame);
        check(SDL_SetRenderLogicalPresentation(renderer.get(),int(movie.width),int(movie.height),SDL_LOGICAL_PRESENTATION_LETTERBOX));
        upload(movie,textures);
        if(movie.name=="tit_load.cf" && cursor_movie && navigation) {
            if(data&&capture.empty()){session=std::make_unique<SessionView>(*data,renderer.get(),false,false,true);session->movie_skip_hold(movie_skip);}
            else title=std::make_unique<TitleController>(*player,*cursor_movie,*navigation);
            upload(*cursor_movie,cursor_textures);
        }
        if(movie.name=="op_title.cf" && option_assets) {
            options=std::make_unique<OptionsController>(*option_assets,settings,app_config(),SDL_GetDisplayForWindow(window.get()));
            for(unsigned i=0;i<frame;++i) options->advance();
        }
        if(movie.name=="bg00.cf" && bonus_assets) {
            bonus=std::make_unique<BonusController>(*bonus_assets,std::nullopt);
            for(unsigned i=0;i<frame;++i) bonus->advance();
        }
        check(SDL_SetWindowTitle(window.get(),window_title.text()));
        std::cout<<"Playing recovered CF timeline: "<<movie.name<<", "<<movie.frames.size()<<" frames at "<<movie.rate<<" fps\n";
    };
    if(option_assets) for(const auto& [name,movie]:option_assets->movies) upload(movie,option_textures[name]);
    if(save_assets) for(const auto* movie:{&save_assets->window,&save_assets->title,&save_assets->input}) upload(*movie,option_textures[movie->name]);
    if(bonus_assets) for(const auto* movie:{&bonus_assets->background,&bonus_assets->selector}) upload(*movie,option_textures[movie->name]);
    const auto return_to_title=[&] {
        const bool from_bonus=bool(bonus);bonus.reset();
        saves.reset();
        if(!title) {
            options.reset();
            const auto found=std::find_if(movies.begin(),movies.end(),[](const auto& movie){return movie.name=="tit_load.cf";});
            if(found==movies.end()) throw std::runtime_error("Missing title return destination");
            selected=unsigned(found-movies.begin());frame=from_bonus?70:67;select();frame=0;return;
        }
        options.reset();player->seek(61+3*title->selection());
        title=std::make_unique<TitleController>(*player,*cursor_movie,*navigation);
        check(SDL_SetWindowTitle(window.get(),window_title.text()));
    };
    select();bool running=true,paused=false;auto previous=SDL_GetTicksNS();Uint64 accumulator=0;
    MenuInput input;
    auto applied_display=app_config();
    set_audio_volumes(applied_display.audio);
    while (running) {
        window_title.advance(window.get());
        SDL_Event event;
        input.fixed_navigation(bool(options));
        while (SDL_PollEvent(&event)) {
            audio_device_event(event);
            if(event.type==SDL_EVENT_QUIT) running=false;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){audio_focused=false;microphone_monitor.stop();if(audio_preview)audio_preview->stop();if(options)options->take_audio_preview();}
            if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)audio_focused=true;
            if(options){
                if(options->event(event,input.gamepad_id())){input.reset();continue;}
                if(event.type==SDL_EVENT_MOUSE_MOTION||event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){SDL_Event converted=event;check(SDL_ConvertEventToRenderCoordinates(renderer.get(),&converted));options->pointer(event.type==SDL_EVENT_MOUSE_MOTION?converted.motion.x:converted.button.x,event.type==SDL_EVENT_MOUSE_MOTION?converted.motion.y:converted.button.y,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT);}
            }
            if(session)session->event(event,input);
            else {
                input.event(event,SDL_GetTicks(),!options&&!saves&&!bonus&&!cinematic&&!attract_fade&&title&&title->phase()==TitleController::Phase::start&&player->frame()==3);
                movie_skip->movie(cinematic?cinematic->playback_generation():0,bool(cinematic)&&audio_focused,SDL_GetTicks());
                if(movie_skip->event(event,input.gamepad_id(),SDL_GetTicks()))input.discard_confirmation();
            }
        }
        const auto finish_attract=[&] {
            cinematic.reset();frame=0;select();accumulator=0;previous=SDL_GetTicksNS();input.reset();
            std::cout<<"Attract movie returned to title\n";
        };
        for(const auto action:input.take(SDL_GetTicks())) {
            if(session) {
                session->action(action);
                continue;
            }
            if(cinematic) {
                continue;
            }
            if(attract_fade) {if(action==MenuAction::back || action==MenuAction::escape){attract_fade=0;finish_attract();}continue;}
            if(action==MenuAction::confirm) {
                if(bonus) {bonus->confirm();if(bonus->request()==BonusController::Request::missing_save) std::cout<<"Bonus preview: save data unavailable\n";}
                else if(saves) saves->confirm();
                else if(options) options->confirm();
                else if(title) {if(title->phase()==TitleController::Phase::start) title->start();else title->confirm();}
            }
            const int horizontal=action==MenuAction::left?-1:action==MenuAction::right?1:0;
            const int vertical=action==MenuAction::up?-1:action==MenuAction::down?1:0;
            if(horizontal || vertical) {
                if(bonus) bonus->move(vertical);
                else if(saves) saves->move(vertical);
                else if(options) options->move(horizontal,vertical);
                else if(title && vertical) {
                    const auto before=title->selection();title->move(vertical);
                    if(before!=title->selection()) std::cout<<"Title selection: "<<TitleController::label(title->selection())<<'\n';
                }
            }
            if(action==MenuAction::back || action==MenuAction::escape) {
                if(bonus) bonus->back();
                else if(saves) saves->back();
                else if(options) options->back();
                else if(title && title->phase()==TitleController::Phase::unavailable) return_to_title();
                else if(title) title->back();
            }
            if(action==MenuAction::pause) paused=!paused;
            if(action==MenuAction::restart) {selected=0;frame=0;select();accumulator=0;input.reset();}
        }
        if(session && running) {const bool was_settings=session->settings_open();session->render();if(was_settings!=session->settings_open())input.reset();applied_display=app_config();if(session->options_requested()&&option_assets){if(!settings_path.empty()){settings=read_menu_settings(settings_path);persisted_settings=settings;}options_session=std::move(session);options=std::make_unique<OptionsController>(*option_assets,settings,app_config(),SDL_GetDisplayForWindow(window.get()));check(SDL_SetRenderLogicalPresentation(renderer.get(),640,448,SDL_LOGICAL_PRESENTATION_LETTERBOX));input.reset();accumulator=0;previous=SDL_GetTicksNS();std::cout<<"Opened PC Options from title script\n";}else if(session->return_to_title()){session.reset();return_to_title();input.reset();}else{check(SDL_RenderPresent(renderer.get()));SDL_Delay(1);continue;}}
        if(cinematic && running) {
            movie_skip->movie(cinematic->playback_generation(),audio_focused,SDL_GetTicks());
            if(!cinematic->advance()||movie_skip->take_skip(SDL_GetTicks())) {finish_attract();movie_skip->movie(0,false,SDL_GetTicks());}
            else {cinematic->render();if(movie_skip->visible()){if(!movie_skip_display)movie_skip_display=std::make_unique<MovieSkipDisplay>(*data,renderer.get());movie_skip_display->render(movie_skip->progress(SDL_GetTicks()));}check(SDL_RenderPresent(renderer.get()));SDL_Delay(1);continue;}
        }
        if(bonus && bonus->request()==BonusController::Request::closed) return_to_title();
        if(saves && saves->phase()==EmptySaveController::Phase::closed) return_to_title();
        // Apply and persist before destroying a closed menu, including edits
        // followed by Escape in the same event batch.
        if(options)options->advance();
        auto desired=options?options->graphics_preview():app_config();
        try{set_audio_device_settings(desired.devices);}catch(const std::exception& error){if(options){options->audio_devices_failed(audio_device_settings(),error.what());desired=options->graphics_preview();}else throw;}
        if(auto error=take_audio_device_error();!error.empty()&&options)options->audio_devices_failed(audio_device_settings(),error);
        set_audio_volumes(desired.audio);
        if(desired!=applied_display){
            try{
                if(desired.mode!=applied_display.mode||desired.width!=applied_display.width||desired.height!=applied_display.height)apply_display_settings(window.get(),desired,!capture.empty());
                if(desired.vsync!=applied_display.vsync)check(SDL_SetRenderVSync(renderer.get(),capture.empty()?0:desired.vsync?1:0));
                set_scene_quality(desired.render_scale,desired.texture_filter,desired.quality);applied_display=desired;
            }catch(const std::exception& error){apply_display_settings(window.get(),app_config(),!capture.empty());SDL_SetRenderVSync(renderer.get(),capture.empty()?0:app_config().vsync?1:0);set_scene_quality(app_config().render_scale,app_config().texture_filter,app_config().quality);applied_display=app_config();applied_display.audio=desired.audio;applied_display.devices=desired.devices;if(options)options->display_failed(error.what());else throw;}
        }
        if(options)options->graphics_applied();
        if(options&&options->graphics()!=app_config()){app_config()=options->graphics();write_app_config(user_config_path(),app_config());}
        if(!settings_path.empty() && settings!=persisted_settings) {
            mono_output=settings.sound==1;
            write_menu_settings(settings_path,settings);persisted_settings=settings;
            std::cout<<"Options saved: controls="<<settings.controls<<", sound="<<settings.sound<<", screen="<<settings.screen_x<<','<<settings.screen_y<<'\n';
        }
        if(audio_preview){
            if(audio_focused&&options&&options->page()==OptionsController::Page::sound){if(auto bus=options->take_audio_preview())audio_preview->play(*bus);audio_preview->update();if(auto error=audio_preview->take_error();!error.empty())options->audio_preview_failed(error);}
            else {if(options)options->take_audio_preview();audio_preview->stop();}
        }
        microphone_monitor.update(running&&audio_focused&&options&&options->page()==OptionsController::Page::microphone);
        if(options)options->microphone_meter(microphone_monitor.level(),microphone_monitor.clipping(),microphone_monitor.listening(),microphone_monitor.error());
        if(options && options->page()==OptionsController::Page::closed){if(options_session){options.reset();session=std::move(options_session);session->finish_options();input.reset();accumulator=0;previous=SDL_GetTicksNS();std::cout<<"PC Options returned to title script\n";}else return_to_title();}
        if (!running) break;
        const auto now=SDL_GetTicksNS(),elapsed=std::min<Uint64>(now-previous,250000000);previous=now;
        if (!paused && capture.empty()) {
            accumulator+=elapsed*player->movie().rate;
            while (accumulator>=1000000000) {
                accumulator-=1000000000;const bool was_playing=player->playing();
                const auto before=title?title->request():TitleController::Request::none;
                if(attract_fade && --attract_fade==0) {
                    if(!data) throw std::runtime_error("Attract playback requires prepared external movie folders. Run build.bat and run.bat.");
                    cinematic=std::make_unique<Cinematic>(*data,renderer.get(),"ts00",false,0,settings.sound==1);
                    check(SDL_SetWindowTitle(window.get(),window_title.text()));
                    std::cout<<"Playing original attract movie: ts00.is\n";accumulator=0;break;
                }
                if(bonus) bonus->advance();else if(saves) saves->advance();else if(options) {}else if(title) title->advance();else player->advance();
                if(title && before!=title->request()) {
                    if(title->request()==TitleController::Request::attract) {
                        attract_fade=60;std::cout<<"Title idle timeout: fading to attract movie\n";
                    } else if(title->request()==TitleController::Request::new_game && data) {
                        session=std::make_unique<SessionView>(*data,renderer.get());
                        session->movie_skip_hold(movie_skip);
                        check(SDL_SetWindowTitle(window.get(),window_title.text()));
                        accumulator=0;input.reset();break;
                    } else if(title->request()==TitleController::Request::options && option_assets) {
                        options=std::make_unique<OptionsController>(*option_assets,settings,app_config(),SDL_GetDisplayForWindow(window.get()));
                        std::cout<<"Opened native Options menu\n";
                        check(SDL_SetWindowTitle(window.get(),window_title.text()));
                    } else if(title->request()==TitleController::Request::load_game && data) {
                        session=std::make_unique<SessionView>(*data,renderer.get(),false,true);
                        session->movie_skip_hold(movie_skip);
                        check(SDL_SetWindowTitle(window.get(),window_title.text()));
                        accumulator=0;input.reset();break;
                    } else if(save_assets && title->request()==TitleController::Request::bonus) {
                        saves=std::make_unique<EmptySaveController>(*save_assets,title->request()==TitleController::Request::bonus);
                        std::cout<<"Opened native empty-save menu\n";
                    check(SDL_SetWindowTitle(window.get(),window_title.text()));
                    } else {
                    const std::string message=std::string(TitleController::label(title->selection()))+" - scene loading is not implemented";
                    std::cout<<"Native title request: "<<message<<'\n';
                    check(SDL_SetWindowTitle(window.get(),window_title.text()));
                    }
                }
                if(was_playing && !player->playing()) std::cout<<"CF timeline stopped at frame "<<player->frame()<<'\n';
                if (!bonus && player->finished() && selected+1<movies.size()) { ++selected;frame=0;select();accumulator=0; }
            }
        }
        if(session) continue;
        // Startup logos keep their authored proportions, while their canvas
        // background and full-canvas solid fades cover the entire drawable.
        const bool wide_logo=player->movie().name=="KOA_logo.cf" || player->movie().name=="complogo.cf";
        std::unique_ptr<WorldOverlayViewport> logo_viewport;
        if(wide_logo)logo_viewport=std::make_unique<WorldOverlayViewport>(renderer.get());
        const auto background=player->background();
        check(SDL_SetRenderDrawColorFloat(renderer.get(),background.x,background.y,background.z,1));
        check(SDL_RenderClear(renderer.get()));
        const auto draws=bonus?bonus->background().compose():saves?saves->window().compose():options?options->root().compose():player->compose();
        const auto render=[&](const std::vector<FlashDraw>& commands,const std::map<unsigned,Texture>& images,float offset_x=0,float offset_y=0) {
        if(!logo_viewport){GeometryBatch geometry(renderer.get());for(const auto& draw:commands)geometry.append(draw.textured?images.at(draw.texture).get():nullptr,draw,[&](const FlashVertex& v){return SDL_FPoint{v.x+offset_x,v.y+offset_y};});geometry.flush();return;}
        for (const auto& draw:commands) {
            std::vector<SDL_Vertex> vertices;vertices.reserve(draw.vertices.size());
            bool canvas=logo_viewport&&draw.vertices.size()==4;
            if(canvas)for(const auto& v:draw.vertices)canvas=canvas&&
                (std::abs(v.x)<.01f||std::abs(v.x-640.f)<.01f)&&
                (std::abs(v.y)<.01f||std::abs(v.y-448.f)<.01f);
            int output_width=0,output_height=0;
            if(canvas)check(SDL_GetRenderOutputSize(renderer.get(),&output_width,&output_height));
            for (const auto& v:draw.vertices){
                SDL_FPoint point{v.x+offset_x,v.y+offset_y};
                if(logo_viewport)point=canvas&&!draw.textured?SDL_FPoint{v.x<320?0.f:float(output_width),v.y<224?0.f:float(output_height)}:logo_viewport->point(point.x,point.y);
                vertices.push_back({point,{v.color.x,v.color.y,v.color.z,v.color.w},{v.u,v.v}});
            }
            check(SDL_RenderGeometry(renderer.get(),draw.textured?images.at(draw.texture).get():nullptr,
                vertices.data(),int(vertices.size()),draw.indices.data(),int(draw.indices.size())));
            if(canvas&&draw.textured){
                // Extend only the border texels into the added canvas. The
                // central logo quad and its authored color/alpha stay intact.
                std::array<SDL_Vertex,4> corner;
                for(unsigned i=0;i<4;++i){const auto& v=draw.vertices[i];corner[(v.y>224?2:0)+(v.x>320?1:0)]=vertices[i];}
                const auto band=[&](SDL_Vertex a,SDL_Vertex b,SDL_Vertex c,SDL_Vertex d){
                    const SDL_Vertex quad[]{a,b,c,d};const int indices[]{0,1,2,0,2,3};
                    check(SDL_RenderGeometry(renderer.get(),images.at(draw.texture).get(),quad,4,indices,6));
                };
                auto tl=corner[0],tr=corner[1],bl=corner[2],br=corner[3];
                auto ltop=tl,lbottom=bl;ltop.position.x=lbottom.position.x=0;
                auto rtop=tr,rbottom=br;rtop.position.x=rbottom.position.x=float(output_width);
                band(ltop,tl,bl,lbottom);band(tr,rtop,rbottom,br);
                auto top_left=ltop,top_right=rtop;top_left.position.y=top_right.position.y=0;
                auto bottom_left=lbottom,bottom_right=rbottom;bottom_left.position.y=bottom_right.position.y=float(output_height);
                band(top_left,top_right,rtop,ltop);band(lbottom,rbottom,bottom_right,bottom_left);
            }
        }
        };
        const auto preview=options?options->preview():settings;
        if(bonus) {
            render(draws,option_textures.at(bonus_assets->background.name),float(preview.screen_x),float(preview.screen_y));
            for(unsigned i=0;i<bonus->rows().size();++i) {
                const auto position=bonus_assets->positions[i+1];
                render(bonus->rows()[i].compose(),option_textures.at(bonus_assets->selector.name),position.x+preview.screen_x,position.y+preview.screen_y);
            }
        } else if(saves) {
            render(saves->title().compose(),option_textures.at(saves->title().movie().name),float(preview.screen_x),float(preview.screen_y));
            render(draws,option_textures.at(saves->window().movie().name),float(preview.screen_x),float(preview.screen_y));
            render(saves->input().compose(),option_textures.at(saves->input().movie().name),float(preview.screen_x),float(preview.screen_y));
        } else if(options) {
            auto& images=option_textures.at(options->root().movie().name);
            upload(options->root().movie(),images);
            render(draws,images,float(preview.screen_x),float(preview.screen_y));
            // Script positions (42,-112) relative to the original centered origin.
            if(options->dialog()) render(options->dialog()->compose(),option_textures.at(options->dialog()->movie().name),362.f+preview.screen_x,112.f+preview.screen_y);
        } else {
            render(draws,textures,float(preview.screen_x),float(preview.screen_y));
            if(title && title->cursor_visible()) render(title->cursor().compose(),cursor_textures,float(preview.screen_x),float(preview.screen_y));
        }
        if(attract_fade) {
            check(SDL_SetRenderDrawColor(renderer.get(),0,0,0,Uint8(255*(60-attract_fade)/60)));
            check(SDL_RenderFillRect(renderer.get(),nullptr));
        }
        if (!capture.empty() && presents>=2) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);
            check(surface!=nullptr);check(SDL_SaveBMP(surface.get(),capture.c_str()));
            std::cout<<"Captured "<<player->movie().name<<" frame "<<player->frame()<<"; draw calls="<<draws.size()<<"\n";
            running=false;
        }
        check(SDL_RenderPresent(renderer.get()));++presents;SDL_Delay(1);
    }
}
}


#include "app/frontend.hpp"
#include "app/session_view.hpp"
#include "platform/menu_input.hpp"
namespace lifeline {
namespace {void check_session_window(bool ok){if(!ok)throw std::runtime_error(std::string("Game display: ")+SDL_GetError());}}
void show_tutorial_intro(const AssetDirectory& assets,const std::string& capture,unsigned page) {
    if(page)throw std::runtime_error("Tutorial page shortcuts were retired");
    check_session_window(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));struct Quit{~Quit(){SDL_Quit();}}quit;
    FrameWindowTitle window_title(game_window_title());
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(create_game_window(window_title.text(),!capture.empty()),SDL_DestroyWindow);check_session_window(bool(window));
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);check_session_window(bool(renderer));
    check_session_window(SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_BLEND));SDL_SetRenderVSync(renderer.get(),capture.empty()&&app_config().vsync?1:0);
    SessionView intro(assets,renderer.get(),!capture.empty());MenuInput input;bool running=true;
    while(running){window_title.advance(window.get());SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_EVENT_QUIT)running=false;intro.event(e,input);}for(auto a:input.take(SDL_GetTicks()))intro.action(a);const bool was_settings=intro.settings_open();intro.render();if(was_settings!=intro.settings_open())input.reset();
        if(intro.return_to_title())running=false;
        if(!capture.empty()){std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);check_session_window(bool(surface));check_session_window(SDL_SaveBMP(surface.get(),capture.c_str()));running=false;}check_session_window(SDL_RenderPresent(renderer.get()));SDL_Delay(1);}
}
}


#include "app/frontend.hpp"
#include "ui/cinematic.hpp"
#include "platform/menu_input.hpp"
#include <deque>
#include <sstream>
namespace lifeline {
namespace {void check_cinematic_window(bool ok){if(!ok)throw std::runtime_error(std::string("Cinematic SDL: ")+SDL_GetError());}}
void show_cinematic(const AssetDirectory& assets,const std::string& name,const std::string& capture,unsigned frame) {
    check_cinematic_window(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));struct Quit {~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(create_game_window("Lifeline - original cinematic",!capture.empty()),SDL_DestroyWindow);check_cinematic_window(bool(window));
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);check_cinematic_window(bool(renderer));
    SDL_SetRenderVSync(renderer.get(),capture.empty()?1:0);
    Cinematic movie(assets,renderer.get(),name,!capture.empty(),frame);MenuInput input;bool running=true,focused=true;MovieSkipHold skip;std::unique_ptr<MovieSkipDisplay> skip_display;
    while(running) {
        skip.movie(movie.playback_generation(),focused&&capture.empty(),SDL_GetTicks());
        SDL_Event event;while(SDL_PollEvent(&event)) {audio_device_event(event);if(event.type==SDL_EVENT_QUIT) running=false;if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)focused=false;if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;input.event(event,SDL_GetTicks());observe_prompt_input(event,input.gamepad_id());skip.event(event,input.gamepad_id(),SDL_GetTicks());}
        input.take(SDL_GetTicks());
        skip.movie(movie.playback_generation(),focused&&capture.empty(),SDL_GetTicks());if(skip.take_skip(SDL_GetTicks()))running=false;
        if(!running || !movie.advance()) break;movie.render();
        if(skip.visible()){if(!skip_display)skip_display=std::make_unique<MovieSkipDisplay>(assets,renderer.get());skip_display->render(skip.progress(SDL_GetTicks()));}
        if(!capture.empty()) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);check_cinematic_window(bool(image));check_cinematic_window(SDL_SaveBMP(image.get(),capture.c_str()));running=false;
        }
        check_cinematic_window(SDL_RenderPresent(renderer.get()));SDL_Delay(1);
    }
    std::cout<<"Cinematic "<<name<<" stopped at picture "<<movie.current_frame()<<'\n';
}
}
