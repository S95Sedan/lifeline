#include "game/session.hpp"
#include "rendering/renderer.hpp"
#include "game/effects/effect_attachments.hpp"
#include "assets/installed_paths.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <SDL3/SDL.h>

namespace lifeline {
namespace {
std::uint32_t original_script_seed(){
    // 1aea30..1aeaa4 seeds registry 8010 from the PS2 CD clock's raw BCD
    // second and minute bytes. Only fall back to the free-running timer when
    // the clock query fails.
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    const bool valid=localtime_s(&local,&now)==0;
#else
    const bool valid=localtime_r(&now,&local)!=nullptr;
#endif
    if(valid)return script::cd_clock_seed(unsigned(local.tm_sec),unsigned(local.tm_min));
    return std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
}
}
GameSession::GameSession(const AssetDirectory& assets,bool silent,Entry entry,const std::filesystem::path& user_directory,std::optional<std::uint32_t> script_seed,std::unique_ptr<SceneRenderer> renderer)
    :assets_(assets),resources_(assets),program_(std::make_shared<script::Program>(entry==Entry::title?"entry_1004.cb":entry==Entry::save?"entry_100.cb":entry==Entry::load?"entry_101.cb":entry!=Entry::tutorial?"entry_013.cb":"entry_1000.cb",assets.read(installed::program(entry==Entry::title?"entry_1004.cb":entry==Entry::save?"entry_100.cb":entry==Entry::load?"entry_101.cb":entry!=Entry::tutorial?"entry_013.cb":"entry_1000.cb")))),
     objects_(resources_),collision_([this](int source,int part){return script::effect_attachment(objects_,camera_.current(),source,part);}),interface_(resources_),text_(assets),inventory_(resources_),script_random_(script_seed.value_or(original_script_seed())),voice_(resources_,silent),sound_(assets,resources_,silent),movies_(assets,silent),drawing_([this](unsigned handle)->const AssetImage&{return movies_.image(handle);}),
     effects_([this]{random_=random_*1103515245+12345;return random_&0x7fffffff;}),effect_resources_(resources_),
     effect_random_([]{const auto ticks=std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());return std::pair{std::uint32_t(ticks),std::uint32_t(ticks>>16)};}),
     live_effects_(effect_resources_,[this](int source,int part){return script::effect_attachment(objects_,camera_.current(),source,part);},[this]{return effect_random_.next();}),
     context_(resources_),analysis_assets_(resources_,interaction_),
     combat_panel_(assets_,combat_targets_,[this](int id){const auto* r=inventory_.record(0,id);if(!r)throw std::runtime_error("Combat inventory record not installed");return r->quantity();},
           [this](int type,int first,int second){if(type==34005)messages_.microphone_position(first,second);else messages_.post(type,first,second);}),
     control_panel_(resources_,command_modes_,[this](int t,int a,int b){messages_.post(t,a,b);},
         [this](bool accepted){
             if(interaction_.view()!=Interaction::View::controls)return;
             interaction_.select(control_panel_.selection()!=0);command_modes_.overlay(2,false,false);analysis_assets_.complete(accepted);
             if(!accepted)command_modes_.focus(1);
             if(accepted){command_modes_.state(0);command_modes_.change(0);}
         },[this](unsigned worker,unsigned address){process_->spawn_from(worker,address);},
         [this](unsigned cue){sound_.play(0,20,cue);}),
     inventory_panel_(resources_,inventory_,false,[this](int t,int a,int b){if(t==34005)messages_.microphone_position(a,b);else messages_.post(t,a,b);},[this](unsigned cue){sound_.play(0,20,cue);}),
     keyword_panel_(resources_,inventory_,true,[this](int t,int a,int b){if(t==34005)messages_.microphone_position(a,b);else messages_.post(t,a,b);},[this](unsigned cue){sound_.play(0,20,cue);}),
     navigator_(resources_,[this](int t,int a,int b){messages_.post(t,a,b);}),renderer_(renderer?std::move(renderer):make_scene_renderer()){
    title_environment_pending_=entry==Entry::title;
    active_scene_=entry==Entry::title?1004:entry==Entry::save?100:entry==Entry::load?101:entry==Entry::tutorial?1000:13;
    messages_.modes(command_modes_);
    drawing_.camera([this]{return camera_.current();});
    drawing_.resources([this](unsigned address){return resources_.addressed(address);});
    if(entry==Entry::after_movie)movies_.skip_next_cinematic();
    live_effects_.contacts([this](math::Vector4 center,float radius,int owner,int mask)->std::optional<script::Effects::Contact>{
        const auto hit=collision_.overlap(center,radius,owner,mask);if(!hit)return {};return script::Effects::Contact{hit->point,hit->normal,hit->material};
    },[this](math::Vector4 first,math::Vector4 second,float radius,int owner,int mask)->std::optional<int>{
        const auto hit=collision_.overlap_capsule(first,second,radius,owner,mask);return hit?std::optional<int>(hit->material):std::nullopt;
    });
    live_effects_.collision([this](math::Vector4 center,float radius,int owner,int mask)->std::optional<int>{
        const auto hit=collision_.overlap(center,radius,owner,mask);return hit?std::optional<int>(hit->material):std::nullopt;
    },[this](unsigned worker,int command,int argument,int material){process_->post(worker,{command,argument,material});});
    live_effects_.viewpoint([this]{return camera_.current().eye;});
    live_effects_.gravity([this](math::Vector4 origin){return objects_.effect_gravity(origin);});
    live_effects_.ground([this](math::Vector4 origin){return objects_.effect_ground(origin);});
    live_effects_.ground_height([this](math::Vector4 origin){return objects_.effect_ground_height(origin);});
    live_effects_.file_loader([this](const std::string& name){return resources_.read_raw("sns/ac.a",name);});
    auto storage_directory=user_directory;
    if(storage_directory.empty()){
        const auto* base=SDL_GetBasePath();
        if(!base)throw std::runtime_error(std::string("Cannot locate installed executable: ")+SDL_GetError());
        storage_directory=std::filesystem::path(base)/"user";
    }
    storage_.configure(resources_,storage_directory);
    context_.on_language_reset([this]{speech_.clear_history();});
    messages_.on_microphone([this](bool value){speech_.unavailable_feedback_enabled(value);});
    messages_.on_reset([this]{microphone_assets_=true;analysis_assets_.message(34003);combat_panel_.message(34003);control_panel_.message(34003);inventory_panel_.message(34003);keyword_panel_.message(34003);navigator_.message(34003);});
    messages_.on_group([this](int group){
        const bool controls=interaction_.view()==Interaction::View::controls;
        analysis_assets_.message(35005,group);
        if(controls&&interaction_.view()!=Interaction::View::controls)command_modes_.overlay(2,false,false);
    });
    interface_.on_reboot([this]{
        control_panel_.release_flash();combat_panel_.release_flash();
        inventory_panel_.release_flash();keyword_panel_.release_flash();
        interaction_.dismiss();microphone_assets_=false;++flash_generation_;
    });
    command_modes_.subscriber(5,{[this](int t,int,int){navigator_.message(t);},[this]{return navigator_.advance();},[this]{return navigator_.available();}});
    command_modes_.subscriber(1,{[this](int t,int a,int b){
        const bool controls=interaction_.view()==Interaction::View::controls;
        analysis_assets_.message(t,a,b);
        // Spoken cancellation can dismiss the inspection directly, without
        // going through the control's pad-confirm/cancel callback.
        if(controls&&interaction_.view()!=Interaction::View::controls)command_modes_.overlay(2,false,false);
    },[this]{return analysis_assets_.advance();}});
    command_modes_.subscriber(2,{[this](int t,int a,int b){control_panel_.message(t,a,b);},[this]{return control_panel_.advance();}});
    command_modes_.subscriber(3,{[this](int t,int a,int b){inventory_panel_.message(t,a,b);},[this]{return inventory_panel_.advance();}});
    command_modes_.subscriber(4,{[this](int t,int a,int b){keyword_panel_.message(t,a,b);},[this]{return keyword_panel_.advance();}});
    command_modes_.subscriber(0,{
        [this](int message,int,int){
            // 165030: base activation queues readiness; close is an observed
            // no-op. The microphone remains active during other command modes.
            if(message==34000)messages_.post(34007,0,0);
            else if(message!=34001)throw std::runtime_error("Base command subscriber protocol not connected");
        },[]{return true;} // 164f60 always returns one; microphone platform/rendering advances in the launcher.
    });
    command_modes_.subscriber(6,{[this](int type,int first,int second){combat_panel_.message(type,first,second,process_ && process_->flags()[400]);},[this]{return combat_panel_.advance();}});
    camera_.resources(resources_);
    collision_.grid_loader([this](const std::string& name){return *resources_.find_raw(name).bytes;});
    camera_.actor_transform([this]{return objects_.actor_world();});
    camera_.object_transform([this](unsigned slot,bool animated_root)->std::optional<math::Matrix4>{
        const auto* object=objects_.instance(slot);if(!object)return {};return object->world_matrix(animated_root);
    });
    // Direct-entry bootstrap supplies the resources normally loaded by startup.
    // It does not replace any conversation worker or authored event transition.
    if(entry==Entry::cell){resources_.load("teru/a/013.a");resources_.load("teru/v/v601.a");resources_.load("teru/bgm/ROOM13.a");}
    // Original title entry_1004.cb 954..9a8 selects both global ITD banks.
    resources_.load("sns/item.a");resources_.load("sns/ana.a");inventory_.select(0,"item.itd");inventory_.select(1,"keyword.itd");
    resources_.load("teru/bgm/mainse.a");
    // The native navigator resolves the shared atlas through the installed
    // resource table, alongside each room's script-selected map textures.
    resources_.load("teru/map.a");
    sound_.select(0,"MAIN");
    if(entry==Entry::cell){voice_.select("601.sv");sound_.select(1,"ROOM13");
    context_.select("os_601.ctx","os_601.wrd");context_.grammar("os_601.vgp");}
    const auto actor=assets.read(installed::rio_model);objects_.actor(decode_skeleton(actor));
    objects_.actor_bank(0,decode_animation_bank(assets.read(installed::rio_animation)));
    if(entry==Entry::cell){objects_.actor_bank(1,decode_animation_bank(assets.read(installed::security_room_rio_animation)));
    objects_.routes(assets.read(installed::security_room_routes));}
    objects_.command_owner([this](script::State* owner){process_->command_owner(owner);});
    objects_.camera_eye([this]{return camera_.current().eye;});
    objects_.sound(sound_);
    movies_.audio_channels([this]{return sound_.reserve_movie_channel();},[this](int handle){sound_.release_movie_channel(handle);});
    sound_.movie_audio_closer([this](int handle){movies_.close_audio_channel(handle);});
    SceneCamera initial;initial.eye={0,0,0,1};initial.target={0,0,1,1};initial.up={0,1,0,0};initial.focal_pixels=544;camera_.initialize(initial);
    auto bindings=create_bindings();
    process_=std::make_unique<script::Process>(program_,script::EventTable(*program_,assets.read(installed::program(entry==Entry::title?"entry_1004.et":entry==Entry::save?"entry_100.et":entry==Entry::load?"entry_101.et":entry!=Entry::tutorial?"entry_013.et":"entry_1000.et"))),bindings,script::Process::Entry::manual);
    process_->inventory_checkpoint([this]{return inventory_.snapshot();},[this](std::span<const std::uint8_t> bytes){inventory_.restore(bytes);});
    process_->saved_inventory_query(script::Inventory::snapshot_value);
    process_->context_storage([this](const std::string& name,Bytes bytes){resources_.store(name,std::move(bytes));},[this](const std::string& name){return resources_.stored(name);});
    process_->file_loader([this](const std::string& name){return *resources_.find_raw(name).bytes;});
    inventory_panel_.flags(process_->flags());keyword_panel_.flags(process_->flags());
    analysis_assets_.connect(process_->flags(),[this](int t,int a,int b){messages_.post(t,a,b);},[this]{return camera_.current();},
        [this](unsigned type){command_modes_.overlay(2,true,true,int(type),0);const auto position=analysis_assets_.position(*interaction_.active(),type+2,camera_.current());if(position)control_panel_.message(34005,int(std::nearbyint(position->x)),int(std::nearbyint(position->y)));control_panel_.advance();},
        [this](unsigned event){const int owner=messages_.owner();if(event){if(owner>=0)process_->dispatch_from(unsigned(owner),event);else process_->dispatch(event);}else{if(owner<0)throw std::runtime_error("Analysis completion requires the originating command worker");process_->post(unsigned(owner),{35011,0,0});}},
        [this](unsigned cue){sound_.play(0,20,cue);});
    interface_.on_message([this](unsigned worker,script::Process::Message message){process_->post(worker,message);});
    if(entry!=Entry::cell){
        resources_.load("sns/ac.a");scene_=std::make_unique<ActorScene>(actor);script_flow_=true;if(entry!=Entry::load&&entry!=Entry::save&&entry!=Entry::title)process_->flags()[2008]=true;
        if(entry==Entry::save)process_->capture_context("__gamecontext");
        // kernel.cb takes its allocation checkpoint at 0xf8, after the saved
        // context and permanent startup banks exist but before loading the
        // selected scene. GameSession hosts that kernel loop natively, so
        // it must preserve the same boundary for every scene handoff.
        begin_scene_resources();
        if(entry==Entry::load)resources_.load("teru/cs/cs101.a");
        if(entry==Entry::save)resources_.load("teru/cs/cs100.a");
        process_->spawn(0);
        // Debug entry advances the actual cinematic wait/cleanup without
        // real-time playback. All authored initialization still executes.
        if(entry==Entry::after_movie){
            while(!movies_.debug_skip_completed()&&frame_<100000)advance();
            if(!movies_.debug_skip_completed())throw std::runtime_error("After-movie debug entry did not reach cinematic cleanup");
        }
        return;
    }
    // Direct-entry setup executes the original room allocation and node setup
    // blocks. Their branch conditions use the same flag bank as story workers.
    script::State setup;setup.flags=process_->flags();
    // The preceding opening event loads the game/enemy resources and dims
    // individual room parts before the cell worker starts. Direct entry must
    // execute those prerequisites too; later events still own their unloads.
    for(const auto [begin,end]:{std::pair{0xf6450u,0xf6480u},std::pair{0xfbe98u,0xfbeb8u},std::pair{0xfbbacu,0xfbc14u},std::pair{0xfbf40u,0xfccd8u},std::pair{0xf6508u,0xf6744u}}){
        script::Runtime runtime(program_,setup,bindings,begin/4);runtime.run_until(end/4);
    }
    // The original room setup selects its ANA records and label/description
    // CFs through the shared CB helper. Run that block for direct entry too.
    {script::Runtime runtime(program_,setup,bindings,0xfd08c/4);runtime.run_until(0xfd128/4);}
    {script::Runtime runtime(program_,setup,bindings,0x5f09c/4);runtime.run_until(0x5f1d0/4);}
    process_->spawn(0xf7a54/4);
}
void GameSession::begin_scene_resources(){
    if(scene_resource_checkpoint_)throw std::logic_error("Scene resource checkpoint is already active");
    resources_.checkpoint();scene_resource_checkpoint_=true;
}
void GameSession::restore_scene_resources(){
    if(!scene_resource_checkpoint_)throw std::logic_error("Scene resource checkpoint is not active");
    resources_.restore_checkpoint();
    // Maps are room resources too. Cinematic-only entries do not install a
    // map; they must not reopen the preceding room's now-unloaded overlay.
    navigator_.reset();
    // AT occupancy is owned by the scene that selected it. The native copy
    // must not survive that scene's archive checkpoint: scenes without an AT
    // selection (including the elevator-hall battle) otherwise query stale
    // geometry from the room that preceded them.
    objects_.reset_navigation_grid();collision_.reset_grid();scene_resource_checkpoint_=false;
}
void GameSession::enter_cell(){
    if(scene_)throw std::logic_error("Cell entry already running");
    scene_=std::make_unique<ActorScene>(assets_.read(installed::security_room_model),assets_.read(installed::rio_model));
    const std::array<unsigned,1> type{1};effects_.bindings().at(830).invoke({*program_,0,830,{},type});
    process_->spawn(0xf69a0/4);
    advance();
}
}


#include "game/session.hpp"
#include "script/resources.hpp"
#include "script/combat_targets.hpp"

namespace lifeline {
script::Bindings GameSession::create_bindings(){
    auto bindings=objects_.bindings();bindings.merge(camera_.bindings());bindings.merge(interface_.bindings());bindings.merge(input_.bindings());
    // Fullscreen FMV input belongs to the host's timed hold. Original movie
    // helpers also poll pad buttons directly; keep those queries neutral so a
    // tap/Back cannot bypass the hold. Native Pause remains independent.
    for(unsigned opcode:{123u,124u}){
        auto original=bindings.at(opcode).invoke;
        bindings.at(opcode).invoke=[this,original](const script::Invocation& call){
            auto reply=original(call);
            if(movies_.fullscreen_visible())call.state->integers[call.operands[2]]=0;
            return reply;
        };
    }
    bindings.merge(script_random_.bindings());
    bindings.merge(collision_.bindings());
    bindings.merge(script::combat_target_bindings(combat_targets_,&combat_panel_));
    bindings.merge(analysis_assets_.bindings());
    bindings.merge(effect_resources_.bindings());
    bindings.merge(live_effects_.bindings());
    bindings.merge(text_.bindings());
    bindings.merge(movies_.bindings());
    bindings.merge(drawing_.bindings());
    bindings.merge(inventory_.bindings());
    bindings.merge(inventory_panel_.bindings());
    bindings.merge(voice_.bindings());bindings.merge(sound_.bindings());bindings.merge(effects_.bindings());bindings.merge(speech_.bindings());
    bindings.merge(context_.bindings());bindings.merge(messages_.bindings());bindings.merge(script::resource_bindings(resources_));
    bindings.merge(control_panel_.bindings());
    bindings.merge(navigator_.bindings());
    bindings.merge(pause_.bindings());
    bindings.merge(storage_.bindings());
    bindings.emplace(491,script::Binding{0,1,[this](const script::Invocation& c){
        const auto bytes=resources_.find_raw(c.program.string(c.operands[0])).bytes;
        // 12e850 selects the same AT data for actor navigation and service 8091.
        objects_.navigation_grid(*bytes);collision_.grid().load(*bytes);return script::Reply{};
    }});
    return bindings;
}
}


#include "game/session.hpp"

namespace lifeline {
bool GameSession::panel_available(){return scene_ && speech_.enabled() && !voice_.speaking() && interaction_.available(messages_.group(),process_->flags());}
bool GameSession::confirm(){
    if(frontend_menu())return true;
    if(pause_.active()||reset_group_>=0)return true;
    if(!(input_.allowed()&0x4000))return false;
    if(inventory_panel_.active()||keyword_panel_.active())return true;
    if(control_panel_.active()){control_panel_.message(34002,6);control_panel_.advance();return true;}
    if(!analysis_assets_.active()){
        if(!panel_available()||!command_modes_.change(1))return false;messages_.advance();
    }
    return analysis_assets_.input(6);
}
bool GameSession::cancel(){
    if(storage_menu()||frontend_menu())return true; // The queued pad edge drives the authored dialog.
    if(pause_.active()||reset_group_>=0)return true;
    if(!(input_.allowed()&0x2000))return false;
    if(navigator_.active()){command_modes_.input(5);return true;}
    if(inventory_panel_.active()||keyword_panel_.active()){command_modes_.state(0);return command_modes_.change(0);}
    if(control_panel_.active()){control_panel_.message(34002,5);control_panel_.advance();return true;}
    if(interaction_.view()==Interaction::View::label){command_modes_.state(0);return command_modes_.change(0);}
    return analysis_assets_.input(5);
}
bool GameSession::panel_direction(int action){
    if(frontend_menu()||storage_menu())return false;
    if(pause_.active()||reset_group_>=0)return false;
    // Item/keyword directions already travel through ScriptPad and the
    // authored command worker. A second frontend dispatch moves twice.
    // Keep that worker in charge of held-button repeat and transition gates.
    if(inventory_panel_.active()||keyword_panel_.active())return false;
    if(control_panel_.active()){control_panel_.message(34002,action);control_panel_.advance();interaction_.select(control_panel_.selection()!=0);return true;}
    return analysis_assets_.input(action);
}
bool GameSession::respond(const std::string& text){
    if(!listening())return false;release_save_return_location();auto parsed=context_.grammar().parse(text);
    if(context_.name().starts_with("os_btl")){pending_battle_speech_.push_back(std::move(parsed));return true;}
    return speech_.submit(parsed);
}
bool GameSession::respond(std::span<const std::string> words){
    if(!listening())return false;release_save_return_location();auto parsed=context_.grammar().parse(words);
    if(context_.name().starts_with("os_btl")){pending_battle_speech_.push_back(std::move(parsed));return true;}
    return speech_.submit(parsed);
}
}


#include "game/session.hpp"

namespace lifeline {
AssetImage GameSession::render(bool transparent,bool high_resolution){
    AssetImage result{};render_into(result,transparent,high_resolution);return result;
}
void GameSession::render_into(AssetImage& image,bool transparent,bool high_resolution){
    if(!scene_)throw std::logic_error("Game scene is not entered");
    // entry_1004 starts effect 11002 several frames before it starts the
    // opaque-black-to-title reveal. Rendering that authored pre-roll exposes
    // a bright, fast-looking starfield by itself. Release it on the first
    // frame of the actual fade; the latch deliberately never affects normal
    // title animation after the reveal.
    if(title_environment_pending_&&interface_.fade().alpha>0)title_environment_pending_=false;
    auto viewport=high_resolution?scene_render_size():std::pair<unsigned,unsigned>{640,448};
    if(!viewport.first)viewport={640,448};
    auto effect_geometry=live_effects_.geometry(camera_.current(),!title_environment_pending_,viewport.first,viewport.second);
    if(std::any_of(effect_geometry.meshes.begin(),effect_geometry.meshes.end(),[](const auto& mesh){return mesh.effect_blend&&(*mesh.effect_blend)[2]==1;}))interface_.append_background(effect_geometry,camera_.current());
    interface_.append_world(effect_geometry);
    drawing_.append_world(effect_geometry,camera_.current());
    auto actor_pose=objects_.current_pose();
    std::vector<math::Matrix4> vent_intro_pose;
    const auto& actor_position=objects_.actor_motion().position;
    // The authored introduction walks on the floor and pauses at the rail
    // before the rail traversal begins. Leave that approach pose untouched.
    if(scene_number()==79 && context_.name()=="os_4802.ctx" && actor_position.z>=.5f && actor_position.z<20.f
       && std::abs(actor_position.y-41.5f)<.05f){
        // The introductory rail animation places Rio's feet at about Y=42
        // while the rail's upper surface is at Y=44.6. Ease the render-only
        // correction in as she steps onto the rail: a full 3-unit offset at
        // Z=0.5 made her visibly teleport upward on the first moving frame.
        // The controlled crossing uses os_4801 and is deliberately unaffected.
        const float progress=std::clamp((actor_position.z-.5f)/3.5f,0.f,1.f);
        const float lift=3.f*progress*progress*(3.f-2.f*progress);
        vent_intro_pose.assign(actor_pose.begin(),actor_pose.end());
        for(auto& bone:vent_intro_pose)bone.columns[3].y+=lift;
        actor_pose=vent_intro_pose;
    }
    scene_->render_into(image,camera_.current(),actor_pose,objects_.actor_visible(),objects_.instance(0),&objects_,&effect_geometry,transparent,renderer_.get(),high_resolution);
    // Opcode 117 is the authored display gate, not only a CF-slot toggle.
    // Opening disables it while constructing the first room before s0.is.
    // Keep primary movies independent: their first decoded picture replaces
    // this black frame after the gate has hidden intermediate scene setup.
    if(interface_.scene_suspended()){
        std::fill(image.rgba.begin(),image.rgba.end(),0);
        for(std::size_t i=3;i<image.rgba.size();i+=4)image.rgba[i]=255;
    }
    movies_.render(image);drawing_.render(image,true);
    // 1671d8 reads Rio's +80 simulation matrix. In cylindrical halls +f0 is
    // the curved render transform and must not be used for the flat floor map.
    navigator_.render(image,objects_.actor_motion().position);effects_.apply(image);text_.render(image);
}
}
