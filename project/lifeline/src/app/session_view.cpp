#include "app/session_view.hpp"
#include "app/config.hpp"
#include "app/settings_overlay.hpp"
#include "game/session.hpp"
#include "ui/script_display.hpp"
#include "ui/presentation_display.hpp"
#include "ui/pause_display.hpp"
#include "ui/hud.hpp"
#include "ui/scene_viewport.hpp"
#include "ui/world_overlay.hpp"
#include "ui/geometry_batch.hpp"
#include "platform/microphone.hpp"
#include "speech/recognizer.hpp"
#include "speech/task.hpp"
#include "platform/script_pad.hpp"
#include "platform/menu_input.hpp"
#include "platform/movie_skip.hpp"
#include "ui/movie_skip_display.hpp"
#include <iostream>
namespace lifeline {
namespace {
void checked(bool ok){if(!ok)throw std::runtime_error(std::string("Session display: ")+SDL_GetError());}
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
}
struct SessionView::Impl {
    SDL_Renderer* renderer;
    AssetDirectory assets;
    GameHud hud;
    bool silent=false,focused=true;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<ScriptInterfaceDisplay> script_interface;
    std::unique_ptr<PresentationDisplay> presentation;
    std::unique_ptr<PauseDisplay> pause_display;
    std::unique_ptr<SettingsOverlay> settings;
    ScriptPad pad;
    std::shared_ptr<MovieSkipHold> movie_skip=std::make_shared<MovieSkipHold>();
    std::unique_ptr<MovieSkipDisplay> movie_skip_display;
    bool movie_confirm_event=false;
    std::uint64_t seen_movie=0;
    bool movie_owned_input=false;
    MicrophoneInput microphone;
    std::uint64_t speech_utterance=0;
    Texture scene_texture{nullptr,SDL_DestroyTexture};
    AssetImage scene_frame{};
    unsigned scene_width=640,scene_height=448;
    unsigned presentation_width=0,presentation_height=0;
    std::unique_ptr<SpeechTask> speech_task;
    unsigned speech_revision=0,room_ticks=0;
    std::optional<Bytes> speech_normalization,speech_fallback;
    SpeechTargetScope speech_target_scope;
    Uint64 room_started=0;
    struct Overlay {
        FlashMovie movie;FlashPlayer player;
        std::map<unsigned,Texture> textures;
        Overlay(const AssetDirectory& assets,const std::string& path,SDL_Renderer* renderer):movie(assets.read(path),path),player(movie){
            for(const auto& [id,image]:movie.textures){Texture t(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);checked(bool(t));checked(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));checked(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));textures.emplace(id,std::move(t));}
        }
        void draw(SDL_Renderer* renderer,float x,float y)const{
            WorldOverlayViewport viewport(renderer);
            GeometryBatch geometry(renderer);for(const auto& batch:player.compose())geometry.append(batch.textured?textures.at(batch.texture).get():nullptr,batch,[&](const FlashVertex& v){return viewport.point(v.x+x,v.y+y);});geometry.flush();
        }
    };
    std::unique_ptr<Overlay> panel_label,panel_description;
    std::map<std::string,std::unique_ptr<Overlay>> analysis_markers;
    Interaction::View panel_view=Interaction::View::closed;
    std::uint64_t flash_generation=0,panel_revision=0;
    Impl(const AssetDirectory& a,SDL_Renderer* r,bool quiet,bool load_game,bool title,std::unique_ptr<GameSession> game={}):renderer(r),assets(a),hud(a,r),silent(quiet){
        if(!silent)checked(SDL_InitSubSystem(SDL_INIT_AUDIO));
        session=game?std::move(game):std::make_unique<GameSession>(assets,silent,title?GameSession::Entry::title:load_game?GameSession::Entry::load:GameSession::Entry::tutorial);
        scene_texture.reset(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,640,448));checked(bool(scene_texture));
        room_started=SDL_GetTicks();
    }
    void confirm(){
      if(movie_confirm_event||(session->fullscreen_movie_visible()&&!session->pause().active()&&!settings)){movie_confirm_event=false;return;}
      if(settings)return; // The panel's fixed menu navigation owns this release.
      if(pad.take_script_confirmation())return;
      if(session->confirm()){
        if(!session->storage_menu()&&!session->frontend_menu())pad.consume(0x4000);
        microphone.cancel();if(speech_task)speech_task->cancel();sync_panel();
    }}
      void sync_panel(){
        if(!session)return;
        if(flash_generation!=session->flash_generation()){
            hud.microphone_assets(false);
            panel_label.reset();panel_description.reset();analysis_markers.clear();panel_view=Interaction::View::closed;
            flash_generation=session->flash_generation();
        }
        hud.microphone_assets(session->microphone_assets());
        const auto& interaction=session->interaction();
        if(interaction.view()==Interaction::View::closed){
            // The original analysis close/group teardown releases marker CFs.
            analysis_markers.clear();panel_view=interaction.view();return;
        }
          const auto label=session->analysis_assets().label_path(),description=session->analysis_assets().description_path();
          if(!panel_label || panel_label->movie.name!=label || panel_description->movie.name!=description){
              panel_label=std::make_unique<Overlay>(assets,label,renderer);
              panel_description=std::make_unique<Overlay>(assets,description,renderer);
              panel_view=Interaction::View::closed;
        }
          if(panel_view!=interaction.view() || panel_revision!=interaction.revision()){
            panel_label->player.seek(0);panel_description->player.seek(0);
            panel_label->player.set_text("text_N02",interaction.active()->label,26,26,{.02734375f,.796875f,.796875f,.5f});
            // 147750..147794 sets the text and dimensions without a tint;
            // retain the CF text service's white default and inline colors.
            panel_description->player.set_text("text_02",interaction.active()->description,26,26);
            // The authored off-panel hotspot expands tw_013's description.
            panel_description->player.pointer(455.f,5.f,false);
        }
          panel_view=interaction.view();
          panel_revision=interaction.revision();
    }
    void prepare_speech(){
        if(!session || silent)return;
        const auto& context=session->context();
        if(!context.ready())return;
        if(!speech_task || speech_revision!=context.revision()){
            microphone.cancel();microphone.discard_events();if(speech_task)speech_task->cancel();
            if(!speech_normalization)speech_normalization=assets.read("speech/engine/normalization.bin");
            const auto* acoustics=context.acoustics();
            if(!acoustics){if(!speech_fallback)speech_fallback=assets.read("speech/engine/acoustics.bin");acoustics=&*speech_fallback;}
            auto engine=std::make_shared<SpeechRecognizer>(*acoustics,context.model(),*speech_normalization,context.acceptance_threshold());
            if(speech_task)speech_task->configure_context(context.vocabulary(),SpeechMode::commands,std::move(engine),context.grammar(),context.revision(),1,context.name());
            else speech_task=std::make_unique<SpeechTask>(context.vocabulary(),SpeechMode::commands,std::move(engine),context.grammar(),speech_backend_configuration(),context.revision(),1,context.name());
            speech_revision=context.revision();speech_target_scope={};
        }
        auto scope=battle_speech_target_scope(session->combat_targets(),session->combat_panel().active());
        if(!(scope==speech_target_scope)){
            // HUD hide/reveal and target updates must not cut off a held
            // utterance. The task retains its capture-time snapshot; this
            // scope is used by the next utterance.
            speech_task->configure_target_scope(scope);
            speech_target_scope=std::move(scope);
        }
    }
    void direction(int direction){
        const auto& control=session->control_panel();
        if((!control.active()||control.type()<=3)&&session->panel_direction(direction)){
            constexpr std::array<std::uint16_t,4> masks{0x10,0x40,0x80,0x20};
            pad.consume(masks[unsigned(direction)]);
        }
        sync_panel();
    }
    void event(const SDL_Event& e,SDL_JoystickID selected,bool native_directions=true) {
        audio_device_event(e);
        observe_prompt_input(e,selected);
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST)focused=false;
        if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;
        sync_movie_skip(SDL_GetTicks());
        movie_confirm_event=movie_skip->event(e,selected,SDL_GetTicks());
        if(movie_confirm_event)return;
        if(settings){if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST)focused=false;if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;settings->event(e);return;}
        const bool menu=session->frontend_menu()||session->storage_menu()||session->inventory_panel().active()||session->keyword_panel().active()||session->map_active()||session->control_panel().active()||session->interaction().view()!=Interaction::View::closed;
        const auto confirm=!session->pause().active()&&!menu&&session->tutorial_message()?std::uint16_t(0x8000):ScriptPad::confirmation(session->requested_buttons(),menu||session->listening(),session->pause().active());
        pad.event(e,selected,session->pause().active()||menu?0x2000:8,confirm,session->title_start_page());
        // MenuInput confirms on release. Consume Cross at key-down too, so
        // holding Enter cannot first send the script's action 5 (Cancel).
        // Storage owns its pad mapping and must still receive confirmation.
        if(!session->storage_menu()&&!session->frontend_menu())pad.consume(0x4000);
        // MenuInput combines sources and opposing directions. A suppressed
        // native move must still consume its physical script-pad copy.
        if(!native_directions&&session->native_panel_navigation())pad.consume(0xf0);
        {
            if(session){
                const int direction=pad.direction_edge();
                if(direction>=0){
                    // Exactly one owner receives a physical direction. Native
                    // inspection selection consumes its queued pad copy;
                    // keypad/choice, item, map and authored room menus return
                    // false and retain that copy for their script worker.
                    if(native_directions)this->direction(direction);
                    return;
                }
            }
            if(silent)return;
            if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;
            if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused=false;microphone.cancel();if(speech_task)speech_task->cancel();return;}
            if(focused && session){
                const bool can_start=session->listening()&&speech_task&&speech_revision==session->context().revision()&&speech_task->ready()&&!speech_task->busy();
                const bool recording=microphone.recording();microphone.event(e,selected,can_start);
                if(!recording && microphone.recording())session->speech_attempt();
            }
            return;
        }
    }
    void sync_movie_skip(Uint64 now){
        const auto generation=session->fullscreen_movie_generation();
        const bool owns=session->fullscreen_movie_visible()&&!settings&&!session->pause().active();
        if(generation&&(generation!=seen_movie||(!movie_owned_input&&owns)))pad.suppress_confirmation();
        seen_movie=generation;movie_owned_input=generation&&owns;
        movie_skip->movie(generation,owns&&focused&&session->fullscreen_movie_skippable(),now,owns);
    }
    std::string status()const {
        const auto confirm=input_prompt(InputAction::confirm),back=input_prompt(InputAction::back),speak=input_prompt(InputAction::speak),navigate=navigation_prompt();
        if(settings)return "Settings | Arrows / Wheel: select | Enter: set | Escape: back";
        if(session->pause().active())return "Paused | "+navigate+": select | "+confirm+": confirm | "+back+": back / resume";
        if(session->storage_menu())return "Save / Load | "+navigate+": select | "+confirm+": confirm | "+back+": back";
        if(session->title_start_page())return "Press any button";
        if(session->tutorial_message())return "Tutorial | "+confirm+": continue | "+input_prompt(InputAction::pause)+": pause";
        if(session->frontend_menu())return "Main menu | "+navigate+": select | "+confirm+": confirm | "+back+": back";
        if(session->voice_panel_active()&&session->listening())return speech_task&&speech_task->busy()?"Recognizing...":"Voice Suite | Hold "+speak+": speak, then release";
        if(session->requested_buttons()&8)return "Press "+confirm+" to continue";
        if(session->inventory_panel().active())return "Items | "+input_prompt(InputAction::items)+": close | "+navigate+": select | "+back+": back";
        if(session->keyword_panel().active())return "Keywords | "+input_prompt(InputAction::keywords)+": close | "+navigate+": select | "+back+": back";
        if(session->map_active())return "Map | "+input_prompt(InputAction::map)+": close | "+back+": back | Hold "+speak+": speak";
        if(session->control_panel().active())return "Control panel | "+navigate+": select | "+confirm+": confirm | "+back+": back";
        if(session->interaction().view()!=Interaction::View::closed)return "Inspection | "+confirm+": operate | "+back+": back";
        if(speech_task&&speech_task->busy())return "Recognizing...";
        return session->listening()?"Hold "+speak+": speak, then release":confirm+": continue / confirm | "+input_prompt(InputAction::pause)+": pause";
    }
    void render() {
        if(session){
            sync_movie_skip(SDL_GetTicks());
            if(movie_skip->take_skip(SDL_GetTicks()))session->skip_fullscreen_movie(session->fullscreen_movie_generation());
            if(settings){
                settings->update();
                if(settings->closed()){session->finish_settings();settings.reset();pad=ScriptPad{};room_started=SDL_GetTicks();room_ticks=0;std::cout<<"In-game Settings closed; gameplay resumed\n";}
            }
            // Treat long loading/suspend time as a paused clock. Keep game
            // updates sequential, with at most eight ticks of catch-up debt.
            const auto now=SDL_GetTicks(),max_elapsed=(Uint64(room_ticks)+8)*1000/60;
            if(now-room_started>max_elapsed)room_started=now-max_elapsed;
            const auto target=std::min<Uint64>((now-room_started)*60/1000,Uint64(room_ticks)+4);
            while(room_ticks<target){
                hud.health(session->player_health());
                if(!session->pause().active())hud.advance();
                if(session){
                    session->microphone_state(microphone.device_state());
                    if(session->control_panel().active()&&session->control_panel().type()==5&&!silent){microphone.prepare();microphone.update();}
                    session->control_panel().microphone_sample(microphone.meter_sample());
                    const bool was_paused=session->pause().active();
                    const auto input=pad.sample_state();session->buttons(input.buttons,input.pause);session->microphone_sample(microphone.meter_sample());session->advance();
                    sync_movie_skip(SDL_GetTicks());
                    if(!was_paused&&session->pause().active()){microphone.cancel();if(speech_task)speech_task->cancel();}
                    if(speech_task&&speech_revision!=session->context().revision()){
                        microphone.cancel();microphone.discard_events();speech_task->cancel();
                    }
                    sync_panel();
                }
                if(panel_view!=Interaction::View::closed&&!session->pause().active()){
                    panel_label->player.advance();panel_description->player.advance();
                    for(auto& [path,overlay]:analysis_markers)overlay->player.advance();
                }
                ++room_ticks;
            }
            if(!session->pause().active())prepare_speech();
            if(session->pause().settings_requested()&&!settings){settings=std::make_unique<SettingsOverlay>(assets,renderer);pad=ScriptPad{};std::cout<<"Opened in-game Settings over the full scene\n";}
            bool backdrop=false;
            for(unsigned slot=0;slot<12;++slot)if(const auto* panel=session->interface().instance(slot);panel&&panel->state!=3&&panel->projection==2&&panel->draw_order<4)backdrop=true;
            int drawable_width=0,drawable_height=0;
            checked(SDL_GetRenderOutputSize(renderer,&drawable_width,&drawable_height));
            if(drawable_width>0&&drawable_height>0){const auto size=scene_resolution(app_config(),unsigned(drawable_width),unsigned(drawable_height));set_scene_drawable_size(size.first,size.second);}
            session->render_into(scene_frame,backdrop&&session->interface().enabled(),true);const auto& picture=scene_frame;
            if(picture.width!=scene_width||picture.height!=scene_height){
                scene_texture.reset(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,int(picture.width),int(picture.height)));checked(bool(scene_texture));
                scene_width=picture.width;scene_height=picture.height;
            }
            const auto [output_width,output_height]=presentation_resolution(app_config(),unsigned(std::max(1,drawable_width)),unsigned(std::max(1,drawable_height)));
            if(!presentation||presentation_width!=output_width||presentation_height!=output_height){presentation=std::make_unique<PresentationDisplay>(renderer,output_width,output_height);presentation_width=output_width;presentation_height=output_height;}
            presentation->begin();
            checked(SDL_UpdateTexture(scene_texture.get(),nullptr,picture.rgba.data(),int(picture.width*4)));
            checked(SDL_SetRenderLogicalPresentation(renderer,640,448,SDL_LOGICAL_PRESENTATION_LETTERBOX));
            checked(SDL_SetRenderDrawColor(renderer,0,0,0,255));checked(SDL_RenderClear(renderer));
            if(!script_interface)script_interface=std::make_unique<ScriptInterfaceDisplay>(renderer,session->interface());
            script_interface->render(ScriptInterfaceDisplay::Layer::background);
            // The software scene stores premultiplied RGB, including translucent
            // surfaces. Composite it over CF bands preceding scene band four.
            checked(SDL_SetTextureBlendMode(scene_texture.get(),SDL_BLENDMODE_BLEND_PREMULTIPLIED));
            const float scene_scale=settings?1.f:std::min(session->inventory_panel().scene_scale(),session->keyword_panel().scene_scale());
            checked(SDL_SetRenderLogicalPresentation(renderer,int(output_width),int(output_height),SDL_LOGICAL_PRESENTATION_DISABLED));
            const auto source_viewport=menu_scene_viewport(picture.width,picture.height,scene_scale);
            const auto output_viewport=menu_scene_viewport(output_width,output_height,scene_scale);
            const SDL_FRect source_rect{source_viewport.source.x,source_viewport.source.y,source_viewport.source.w,source_viewport.source.h};
            const SDL_FRect scene_rect{output_viewport.destination.x,output_viewport.destination.y,output_viewport.destination.w,output_viewport.destination.h};
            checked(SDL_RenderTexture(renderer,scene_texture.get(),&source_rect,&scene_rect));
            // Retain the existing 640x448 HUD layout, independently of the
            // expanded 3D framebuffer. HUD redesign is deliberately separate.
            checked(SDL_SetRenderLogicalPresentation(renderer,640,448,SDL_LOGICAL_PRESENTATION_LETTERBOX));
            {
                if(!script_interface)script_interface=std::make_unique<ScriptInterfaceDisplay>(renderer,session->interface());
                script_interface->render_effects(session->live_effects());
                script_interface->render_combat(session->combat_panel(),session->camera());
                script_interface->render(ScriptInterfaceDisplay::Layer::foreground);
                const bool ready=focused && session && session->listening() && speech_task && speech_revision==session->context().revision() && speech_task->ready() && !speech_task->busy();
                if(ready)microphone.prepare();
                // A hit or dialogue can disable listening between press and
                // release. Drain the device and finish the release tail even
                // then, instead of extending one utterance across the stun.
                microphone.update();
                while(auto event=microphone.take_event()){
                    if(!speech_task)continue;
                    switch(event->type){
                    case MicrophoneUtteranceEvent::Type::begin:
                        speech_utterance=speech_task->begin()?event->utterance:0;
                        if(speech_utterance&&!event->samples.empty())speech_task->accept_audio(event->samples);
                        break;
                    case MicrophoneUtteranceEvent::Type::audio: if(event->utterance==speech_utterance)speech_task->accept_audio(event->samples);break;
                    case MicrophoneUtteranceEvent::Type::end: if(event->utterance==speech_utterance)speech_task->finish_input();break;
                    case MicrophoneUtteranceEvent::Type::cancel: speech_utterance=0;speech_task->cancel();break;
                    }
                }
                // Legacy callers can still consume a whole capture; the game
                // now uses ordered events, so discard its compatibility copy.
                microphone.take_capture();
                if(speech_task&&!session->pause().active())if(auto result=speech_task->take()){
                    const auto score=speech_score(*result);
                    const auto threshold=session->context().acceptance_threshold();
                    const bool current=result->context_generation==session->context().revision()&&session->listening();
                    if(current&&score)session->recognition_score(std::uint16_t(*score),result->word_id.has_value() || !result->text.empty());
                    const bool accepted=result->status==SpeechResultStatus::final&&result->error.empty()&&result->rejection==SpeechRejection::none&&score&&*score>=threshold;
                    if(current&&accepted&&(result->components.empty()?session->respond(result->text):session->respond(result->components))){
                        std::cout<<"Speech: "<<result->text;
                        std::cout<<" | "<<session->context().name()<<" | "<<speech_backend_name(result->backend)
                                 <<(score?std::string(" score ")+std::to_string(*score):std::string(" accepted"))<<'\n';
                    }else {
                        // 1438f4: low confidence; 143954: invalid input;
                        // 143828: recognized sequence with no grammar rule.
                        if(current&&result->error.empty())session->recognition_failure(script_speech_failure(result->rejection,accepted));
                        std::cout<<(result->error.empty()?"Speech rejected: ":"Speech engine error: ")<<result->text<<" | "<<session->context().name()<<" | "<<(result->error.empty()?result->diagnostic:result->error)<<'\n';
                    }
                }
                const auto mic_display=session->microphone_display();
                hud.vertical_offset(session->microphone_offset());hud.score(false,int(session->microphone_score()));
                hud.microphone(!session->microphone_visible()?MicrophoneDisplay::hidden:mic_display==1?MicrophoneDisplay::cannot_talk:mic_display==2?MicrophoneDisplay::talk_now:mic_display==3?MicrophoneDisplay::disconnected:MicrophoneDisplay::idle);
                hud.spectrum(microphone.spectrum());
                const bool inventory_hud=session->inventory_panel().active()||session->keyword_panel().active();
                if(!inventory_hud)hud.render();
                sync_panel();
                if(panel_view!=Interaction::View::closed){
                    auto& analysis=session->analysis_assets();const auto& selected=*session->interaction().active();
                    for(const auto* record:analysis.records())if(const auto position=analysis.position(*record,0,session->camera())){
                        for(unsigned layer=0;layer<(record==&selected?2u:1u);++layer){const auto path=analysis.marker_path(record->kind(),layer!=0);auto& overlay=analysis_markers[path];if(!overlay){overlay=std::make_unique<Overlay>(assets,path,renderer);overlay->player.looping(true);}overlay->draw(renderer,position->x,position->y);}
                    }
                    if(const auto position=analysis.position(selected,1,session->camera()))panel_label->draw(renderer,position->x,position->y);
                    if(panel_view==Interaction::View::description||panel_view==Interaction::View::controls)if(const auto position=analysis.position(selected,2,session->camera()))panel_description->draw(renderer,position->x,position->y);
                }
                script_interface->render_control(session->control_panel());
                script_interface->render_inventory(session->inventory_panel());script_interface->render_inventory(session->keyword_panel());
                // The list's band-8 black backing must precede the mic frame.
                if(inventory_hud)hud.render();
                const auto fade=std::clamp(session->fade().alpha/128.f,0.f,1.f);
                if(fade>0){
                    // Transition fades cover the extended backgrounds too.
                    checked(SDL_SetRenderLogicalPresentation(renderer,int(picture.width),int(picture.height),SDL_LOGICAL_PRESENTATION_DISABLED));
                    checked(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND));checked(SDL_SetRenderDrawColorFloat(renderer,0,0,0,fade));checked(SDL_RenderFillRect(renderer,nullptr));
                    checked(SDL_SetRenderLogicalPresentation(renderer,640,448,SDL_LOGICAL_PRESENTATION_LETTERBOX));
                }
            }
            presentation->finish(session?session->presentation_mode():0,session?session->display_offset()[0]:0,session?session->display_offset()[1]:0);
            if(movie_skip->visible()){
                if(!movie_skip_display)movie_skip_display=std::make_unique<MovieSkipDisplay>(assets,renderer);
                movie_skip_display->render(movie_skip->progress(SDL_GetTicks()));
            }
            if(settings)settings->render();
            else if(session&&session->pause().active()){
                if(!pause_display)pause_display=std::make_unique<PauseDisplay>(assets,renderer);
                pause_display->render(session->pause());
            }
            return;
        }
    }
};
SessionView::SessionView(const AssetDirectory& a,SDL_Renderer* r,bool silent,bool load_game,bool title):impl_(std::make_unique<Impl>(a,r,silent,load_game,title)){}
SessionView::SessionView(const AssetDirectory& a,SDL_Renderer* r,std::unique_ptr<GameSession> game,bool silent):impl_(std::make_unique<Impl>(a,r,silent,false,false,std::move(game))){}
SessionView::~SessionView()=default;
void SessionView::confirm(){impl_->confirm();}
bool SessionView::cancel(){if(impl_->settings)return true;if(impl_->session->cancel()){
    if(!impl_->session->storage_menu()&&!impl_->session->frontend_menu()&&!impl_->session->pause().active())impl_->pad.consume(0x2000);
    impl_->sync_panel();return true;
}return false;}
void SessionView::event(const SDL_Event& e,SDL_JoystickID id){impl_->event(e,id);}
void SessionView::event(const SDL_Event& e,MenuInput& input){
    input.event(e,SDL_GetTicks());impl_->event(e,input.gamepad_id(),false);
    if(impl_->movie_confirm_event)input.discard_confirmation();
    // Deliver releases/Back before the next event changes native selection.
    for(const auto next:input.take(SDL_GetTicks()))action(next);
}
void SessionView::action(MenuAction next){
    if(impl_->settings)return;
    if(next==MenuAction::confirm)confirm();
    else if(next==MenuAction::back||next==MenuAction::escape)cancel();
    else if(next>=MenuAction::up&&next<=MenuAction::right)impl_->direction(int(next));
}
std::string SessionView::status()const{return impl_->status();}
void SessionView::render(){impl_->render();}
void SessionView::movie_skip_hold(std::shared_ptr<MovieSkipHold> hold){if(hold)impl_->movie_skip=std::move(hold);}
bool SessionView::return_to_title()const{return impl_->session->return_to_title();}
bool SessionView::options_requested()const{return impl_->session->options_menu();}
bool SessionView::settings_open()const{return bool(impl_->settings);}
void SessionView::finish_options(){impl_->session->finish_options();impl_->pad=ScriptPad{};impl_->microphone.cancel();impl_->room_started=SDL_GetTicks();impl_->room_ticks=0;}
}
