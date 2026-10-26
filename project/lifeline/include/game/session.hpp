#pragma once
#include "script/storage.hpp"
#include "script/process.hpp"
#include "game/objects.hpp"
#include "script/camera.hpp"
#include "script/interface.hpp"
#include "script/text.hpp"
#include "script/movies.hpp"
#include "script/drawing.hpp"
#include "script/inventory.hpp"
#include "script/input.hpp"
#include "script/random.hpp"
#include "script/voice_bank.hpp"
#include "script/sound.hpp"
#include "game/effects/screen_effects.hpp"
#include "game/effects/effect_resources.hpp"
#include "game/effects/effects.hpp"
#include "game/effects/effect_random.hpp"
#include "script/speech_input.hpp"
#include "script/speech_context.hpp"
#include "script/command_messages.hpp"
#include "script/collision.hpp"
#include "game/scene.hpp"
#include "ui/interaction.hpp"
#include "ui/combat_targets.hpp"
#include "ui/combat_panel.hpp"
#include "ui/control_panel.hpp"
#include "ui/inventory_panel.hpp"
#include "ui/navigator.hpp"
#include "ui/pause.hpp"
#include "script/analysis_assets.hpp"
#include <deque>

namespace lifeline {
// Owns native services; story branches, waits and replies stay in installed CB.
class GameSession {
    script::Storage storage_;
    AssetDirectory assets_;
    Resources resources_;
    std::shared_ptr<script::Program> program_;
    script::Objects objects_;
    script::Collision collision_;
    script::Camera camera_;
    script::Interface interface_;
    script::Text text_;
    script::Inventory inventory_;
    script::Input input_;
    script::Random script_random_;
    script::VoiceBank voice_;
    script::Sound sound_;
    script::Movies movies_;
    script::Drawing drawing_;
    std::uint32_t random_=1;
    script::ScreenEffects effects_;
    script::EffectResources effect_resources_;
    script::EffectRandom effect_random_;
    script::Effects live_effects_;
    script::SpeechInput speech_;
    script::SpeechContext context_;
    script::CommandMessages messages_;
    Interaction interaction_;
    script::AnalysisAssets analysis_assets_;
    CombatTargets combat_targets_;
    CombatPanel combat_panel_;
    script::CommandModes command_modes_;
    ControlPanel control_panel_;
    InventoryPanel inventory_panel_,keyword_panel_;
    Navigator navigator_;
    Pause pause_;
    std::optional<bool> pause_input_;
    std::int32_t reset_group_=-1;
    std::unique_ptr<script::Process> process_;
    std::unique_ptr<ActorScene> scene_;
    std::unique_ptr<SceneRenderer> renderer_;
    std::uint64_t frame_=0;
    std::uint64_t flash_generation_=0;
    // Original constructor clears the four CF pointers; reset loads them.
    bool microphone_assets_=false;
    bool script_flow_=false,title_requested_=false;
    // The title script creates its animated environment before starting the
    // black reveal. Keep that pre-roll hidden, then leave the authored title
    // animation alone for the rest of the session.
    bool title_environment_pending_=false;
    std::int32_t scene_group_=1;
    int active_scene_=13;
    std::optional<std::pair<math::Vector4,math::Vector4>> save_return_location_;
    bool restoring_save_location_=false;
    bool pending_floating_load_=false;
    bool pending_route_entry_heading_=false;
    bool pending_post_brain_exit_arrival_=false;
    unsigned save_return_idle_=0;
    bool laptop_animation_hold_=false,laptop_command_worker_seen_=false;
    bool scene_resource_checkpoint_=false;
    std::deque<CommandGrammar::Parsed> pending_battle_speech_;
    void begin_scene_resources();
    script::Bindings create_bindings();
    void restore_scene_resources();
    void release_save_return_location(){restoring_save_location_=false;save_return_location_.reset();}
public:
    enum class Entry { cell, tutorial, story, after_movie, load, save, title };
    explicit GameSession(const AssetDirectory&,bool silent=false,Entry entry=Entry::cell,const std::filesystem::path& user_directory={},std::optional<std::uint32_t> script_seed={},std::unique_ptr<SceneRenderer> renderer={});
    void microphone_sample(std::int16_t sample){live_effects_.microphone(sample,(input_.buttons()&0x2000)!=0);}
    void microphone_state(int value){speech_.device_state(value);}
    void enter_cell();
    void advance();
    std::uint64_t fullscreen_movie_generation()const{return movies_.fullscreen_generation();}
    bool fullscreen_movie_visible()const{return movies_.fullscreen_visible();}
    bool fullscreen_movie_skippable()const{return movies_.fullscreen_skippable();}
    unsigned fullscreen_movie_frame()const{return movies_.fullscreen_frame();}
    bool skip_fullscreen_movie(std::uint64_t generation){return movies_.request_skip_fullscreen(generation);}
    const Pause& pause()const{return pause_;}
    void finish_settings(){pause_.finish_settings();input_.buttons(0);advance();}
    bool return_to_title()const{return title_requested_||(reset_group_>=0&&process_->group_result(reset_group_)==1004);}
    bool frontend_menu()const{return active_scene_>=1001;}
    bool title_start_page()const{
        if(active_scene_!=1004||!requested_buttons())return false;
        for(unsigned slot=0;slot<12;++slot)if(const auto* panel=interface_.instance(slot);panel&&panel->movie.name=="tit_load.cf"&&panel->player.frame()==3)return true;
        return false;
    }
    bool tutorial_message()const{
        if(active_scene_!=1000||!interface_.enabled())return false;
        for(unsigned slot=0;slot<12;++slot)if(const auto* panel=interface_.instance(slot);panel&&panel->state!=3&&panel->movie.name=="msg.cf"){
            // The textbox owns Confirm while its text/hint animates too.
            // A hidden hint must not fall back to a stale title Start request.
            return true;
        }
        return false;
    }
    bool options_menu()const{return active_scene_==1017;}
    void finish_options(){if(!options_menu())throw std::logic_error("No PC Options scene to close");process_->complete_scene(scene_group_,1004);input_.buttons(0);advance();}
    bool storage_menu()const{return active_scene_==100||active_scene_==101;}
    void buttons(std::uint16_t held,std::optional<bool> pause_input={}){
        pause_input_=pause_input;
        if(active_scene_==1004&&(held&0x2000)){
            // Circle/Escape must never become one of title's confirm buttons.
            if(process_->title_back()){interface_.title_start_page();live_effects_.title_start_page();input_.clear_requests();}
            held=std::uint16_t(held&~0x6000u);
        }
        // Keep Enter/Cross = confirm and Escape/Circle = back in the PC
        // menus. The authored storage UI consumes Square/Cross respectively.
        if(frontend_menu()&&requested_buttons()==8&&(held&0x4000))held=std::uint16_t((held&~0x4000u)|8);
        if(storage_menu()||frontend_menu()){
            const unsigned cancel=frontend_menu()&&!(requested_buttons()&0x4000)?0x2000:0x4000;
            held=std::uint16_t((held&~0x6000u)|((held&0x4000u)?0x8000u:0)|((held&0x2000u)?cancel:0));
        }
        input_.buttons(held);
    }
    AssetImage render(bool transparent=false,bool high_resolution=false);
    void render_into(AssetImage&,bool transparent=false,bool high_resolution=false);
    std::uint16_t requested_buttons()const{return input_.requested_presses();}
    int presentation_mode()const{return drawing_.presentation_mode();}
    bool voice_panel_active()const{return (control_panel_.active()&&control_panel_.type()==5)||live_effects_.active_type(11001);}
    // 143428 branches to recognition without consulting speech-input +90.
    // That flag controls unavailable-input feedback (1434cc), not capture.
    // Hidden-HUD dialogue, including the ending password, still accepts speech.
    bool listening()const{const bool voice_panel=voice_panel_active();return !pause_.active()&&reset_group_<0&&speech_.enabled() && !control_panel_.blocks_speech() && !inventory_panel_.active() && (voice_panel||analysis_assets_.active()||interaction_.view()==Interaction::View::closed||interaction_.view()==Interaction::View::label);}
    bool map_active()const{return navigator_.active();}
    bool microphone_enabled()const{return messages_.microphone_enabled();}
    bool microphone_assets()const{return microphone_assets_;}
    bool microphone_visible()const{return microphone_assets_&&messages_.microphone_enabled();}
    // 164f60 / 165458: hardware and speech gates override the script's
    // selected panel. Frontend menu focus is not a CANNOT TALK request.
    int microphone_display()const{return speech_.device_state()!=2?3:!speech_.enabled()?1:messages_.display();}
    std::uint64_t flash_generation()const{return flash_generation_;}
    bool panel_available();
    bool confirm();
    bool cancel();
    // True when the native inspection frontend owned the direction. False
    // leaves it queued for the authored room/menu worker.
    bool panel_direction(int action);
    bool native_panel_navigation()const{
        if(frontend_menu()||storage_menu()||pause_.active()||reset_group_>=0||inventory_panel_.active()||keyword_panel_.active())return false;
        if(control_panel_.active())return control_panel_.type()>=1&&control_panel_.type()<=3;
        return analysis_assets_.active()&&(interaction_.view()==Interaction::View::label||interaction_.view()==Interaction::View::description);
    }
    void select_panel(bool open){panel_direction(open?2:3);}
    const Interaction& interaction()const{return interaction_;}
    const script::AnalysisAssets& analysis_assets()const{return analysis_assets_;}
    script::AnalysisAssets& analysis_assets(){return analysis_assets_;}
    const script::Interface& interface()const{return interface_;}
    CombatPanel& combat_panel(){return combat_panel_;}
    const CombatTargets& combat_targets()const{return combat_targets_;}
    const script::Effects& live_effects()const{return live_effects_;}
    script::Effects& live_effects(){return live_effects_;}
    bool collision_grid_loaded()const{return collision_.grid().loaded();}
    ControlPanel& control_panel(){return control_panel_;}
    InventoryPanel& inventory_panel(){return inventory_panel_;}
    InventoryPanel& keyword_panel(){return keyword_panel_;}
    const SceneCamera& camera()const{return camera_.current();}
    const script::Objects& scene_objects()const{return objects_;}
    const std::array<int,2>& display_offset()const{return camera_.display_offset();}
    std::optional<int> player_health()const{const auto* record=inventory_.record(0,1);return record?std::optional<int>(record->fields[2]):std::nullopt;}
    float microphone_offset()const{return float(messages_.microphone_y()+220);}
    bool respond(const std::string&);
    bool respond(std::span<const std::string> words);
    void recognition_failure(int result){if(result!=-2&&result!=-3&&result!=-4)throw std::logic_error("Invalid recognition failure");if(listening())speech_.submit(result);}
    // Keep the last score visible during capture; replace it with a result.
    void speech_attempt(){inventory_.speech_attempt();}
    void recognition_score(std::uint16_t value,bool has_words=true){speech_.score(value,has_words);if(has_words){inventory_.speech_score(value);messages_.post(36000,value,0);}}
    unsigned microphone_score()const{return messages_.microphone_score();}
    unsigned voice()const{return voice_.voice();}
    std::size_t queued_battle_speech()const{return pending_battle_speech_.size();}
    bool speaking()const{return voice_.speaking();}
    bool sound_effect_active(unsigned bank,unsigned group,unsigned program,unsigned key)const{return sound_.effect_active(bank,group,program,key);}
    const script::SpeechContext& context()const{return context_;}
    const script::Interface::Fade& fade()const{return interface_.fade();}
    auto binding_shapes()const{return process_->binding_shapes();}
    auto script_locations()const{return process_->locations();}
    int scene_number()const{return process_->scene_number();}
    bool resource_loaded(const std::string& archive)const{return resources_.loaded(archive);}
    auto game_context()const{return process_->context_snapshot();}
#ifdef LIFELINE_DEV_SCENE_PROBE
    std::uint64_t debug_frame()const{return frame_;}
    int debug_speech_result()const{return speech_.result();}
    void debug_scene_flag(unsigned id,bool value){process_->flags()[id]=value;}
    void debug_scene_flags(const std::map<std::uint32_t,bool>& flags){process_->flags().restore(flags);}
    unsigned debug_camera_mode()const{return camera_.mode();}
    void debug_observe_bindings(std::span<const unsigned> opcodes,std::function<void(const script::Invocation&)> observer){process_->debug_observe_bindings(opcodes,std::move(observer));}
    const auto& debug_screen_effects()const{return effects_;}
    std::string debug_map()const{return navigator_.debug_description();}
    void debug_enable_voice_audio(){voice_.debug_enable_audio();}
    unsigned debug_dispatch_scene_event(unsigned event){return process_->dispatch(event);}
    unsigned debug_spawn_scene_address(std::uint32_t byte_address){return process_->spawn(byte_address/4);}
    float debug_scene_data(std::uint32_t word)const{return process_->inspect_data(scene_group_,word);}
    const auto& debug_collisions()const{return collision_.records();}
    std::array<bool,4> debug_speech_gates()const{return {speech_.enabled(),speech_.unavailable_feedback_enabled(),inventory_panel_.active(),control_panel_.blocks_speech()};}
    void debug_enter_authored_scene(unsigned scene){
        for(unsigned slot=0;slot<24;++slot)process_->stop(slot);
        process_->retire_scene(scene_group_);camera_.end_scene();restore_scene_resources();
        if(scene==100){process_->flags()[2000]=true;process_->capture_context("__gamecontext");}
        begin_scene_resources();
        auto number=std::to_string(scene);while(number.size()<3)number='0'+number;
        resources_.load("teru/cs/cs"+number+".a");objects_.begin_route_context();
        scene_group_=process_->start_scene("entry_"+number+".cb");active_scene_=scene;
    }
#endif
};
}
