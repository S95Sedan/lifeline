#include "game/session.hpp"
#include "script/memory.hpp"
#include "script/resources.hpp"
#include "assets/installed_paths.hpp"

namespace lifeline {
void GameSession::advance(){
    const bool was_paused=pause_.active();
    auto pause_buttons=was_paused?std::uint16_t(input_.mapped_buttons()&0xa0f8):input_.buttons();
    // Script Start can mean "continue". Only a host Pause/Back action may
    // open Pause; retain raw-pad behavior for script/recovery callers.
    if(pause_input_&&!*pause_input_)pause_buttons=std::uint16_t(pause_buttons&~8u);
    pause_.advance(pause_buttons);
    if(pause_input_.value_or(false))input_.consume(8);
    if(was_paused!=pause_.active()){voice_.pause(pause_.active());sound_.pause(pause_.active());movies_.pause(pause_.active());}
    if(pause_.active())return;
    // Continue restores the suspended frame before polling gameplay again.
    // Consume the menu's confirmation edge in the pad history.
    if(was_paused&&!pause_.reset_requested()){input_.advance();return;}
    if(pause_.reset_requested()&&reset_group_<0){
        resources_.load("teru/cs.a");reset_group_=process_->start_external("reset.cb");
        if(reset_group_<0)throw std::runtime_error("No worker available for authored reset script");
    }
    drawing_.begin_frame();
    input_.advance();voice_.advance();movies_.advance();
    // os_2705 handles Take laptop (1018) itself and starts its authored
    // command worker directly. Feeding the same result to automatic route
    // dispatch leaves a mode-7 deferred copy, replaying Allen's block after
    // the conversation action completes.
    const bool laptop_command=context_.name()=="os_2705.ctx"&&speech_.result()==1018;
    if(laptop_command&&!laptop_animation_hold_){
        objects_.hold_completed_object_animation(1,true);
        laptop_animation_hold_=true;laptop_command_worker_seen_=false;
    }
    const int actor_command=laptop_command?-1:speech_.result();
    if(active_scene_==37)objects_.testing_room_exit_command(actor_command);
    // An explicit command takes ownership immediately; a pending arrival
    // must never stop Rio later in the middle of that requested movement.
    if(actor_command>=0)pending_post_brain_exit_arrival_=false;
    objects_.command_input(actor_command);
    if(const auto& face=voice_.facial_expression()){
        const bool preserve_eyes=objects_.actor_expression().preserve_eyes;
        objects_.actor_expression()=*face;objects_.actor_expression().preserve_eyes=preserve_eyes;
    }
    if(const auto event=objects_.update_commands(process_->active(23)))process_->dispatch_command(*event);
    process_->advance();
    // Apply an alternate entrance's direction once its route is installed,
    // before root motion advances. Otherwise the authored main-passage yaw
    // makes the first swimming frames carry Rio into the side wall.
    if(pending_route_entry_heading_&&context_.name()=="ai_094.ctx"&&objects_.orient_scene_entry())
        pending_route_entry_heading_=false;
    if(pending_post_brain_exit_arrival_&&context_.name()=="ai_094.ctx"&&
        speech_.enabled()&&objects_.finish_exit_arrival())
        pending_post_brain_exit_arrival_=false;
    const bool battle_context=context_.name().starts_with("os_btl");
    // Battle workers can be suspended during a turn, hit or grapple. Keep an
    // unread result available until a worker polls it; non-battle delivery
    // retains the original one-frame latch lifetime.
    // Dialogue workers can yield between polls too. A failed but nonempty
    // utterance must remain visible until their next 597/598 read; otherwise
    // free-response prompts discard it depending on the update phase.
    speech_.finish_frame(battle_context||speech_.result()==-2||speech_.result()==-4);
    if(battle_context){
        // The script's speech gate and single result latch own readiness.
        // Facing, voice playback and target HUD messages are not command
        // acknowledgements: waiting for them can strand every later word.
        const bool target_command=!pending_battle_speech_.empty()&&std::any_of(
            pending_battle_speech_.front().sequence.begin(),pending_battle_speech_.front().sequence.end(),
            [](int word){return word==1||word==100||(word>=16&&word<=18)||(word>=60&&word<=92);});
        // The authored receiver discards target commands during actor-mode
        // transitions or before its selected enemy exists. Retain the original
        // utterance until those semantic preconditions hold, not until a HUD
        // message, voice cue, or facing-angle threshold happens to change.
        if(!pending_battle_speech_.empty()&&listening()&&speech_.result()==-1&&
            (!target_command||process_->battle_target_command_ready(scene_group_))&&
            speech_.submit(pending_battle_speech_.front())){
            pending_battle_speech_.pop_front();
        }
    }else{
        pending_battle_speech_.clear();
    }
    if(laptop_animation_hold_){
        laptop_command_worker_seen_|=process_->active(23);
        if(laptop_command_worker_seen_&&!process_->active(23)){
            // The close-up is over: now apply the script's deferred idle
            // restart, after the held final blocking pose is off camera.
            objects_.hold_completed_object_animation(1,false);
            laptop_animation_hold_=laptop_command_worker_seen_=false;
        }
    }
    if(pending_floating_load_&&(context_.name()=="ai_094.ctx"||context_.name()=="ai_037.ctx")&&listening()){
        // The zero-gravity corridor and testing room use rest animation 92. The
        // storage presentation's ordinary standing clip is not a valid idle
        // here, including when the session starts from the title's Load menu.
        if(objects_.animation()==0||objects_.animation()==137){
            const auto motion=objects_.actor_motion();
            objects_.restore_actor_location(motion.position,motion.rotation,true,92);
        }
        pending_floating_load_=false;
    }
    if(restoring_save_location_&&save_return_location_){
        // Data Save is a temporary scene. Re-entering the room reconstructs
        // its services, but must not replace Rio's save-point placement with
        // the room's generic revisit route.
        objects_.restore_actor_location(save_return_location_->first,save_return_location_->second,true,save_return_idle_);
        // Keep Rio in the room's idle at the save point until the next room
        // command. Some room revisit workers select seated animation 137 after
        // speech is already enabled, so releasing here would expose that load-
        // only pose after an in-game save.
    }
    objects_.advance();
    collision_.advance();camera_.advance(objects_.current_pose(),objects_.actor_world());effects_.advance(frame_++);sound_.advance();interface_.advance();messages_.advance();text_.advance();
    if(title_environment_pending_&&interface_.fade().alpha>0)title_environment_pending_=false;
    objects_.update_room_visibility(camera_.current().eye);
    live_effects_.advance();command_modes_.advance();
    // Direct 860 initialization also owns an updating combat service, even
    // when it was not activated through a command-mode transition.
    if(!command_modes_.active(6) && combat_panel_.active())combat_panel_.advance();
    if(!command_modes_.active(2))control_panel_.advance();
    if(!command_modes_.active(5))navigator_.advance();
    if(!command_modes_.active(3)&&inventory_panel_.active())inventory_panel_.advance();
    if(!command_modes_.active(4)&&keyword_panel_.active())keyword_panel_.advance();
    storage_.advance();
    if(script_flow_&&!title_requested_&&!process_->group_running(scene_group_)){
        const auto scene_result=process_->group_result(scene_group_);process_->retire_scene(scene_group_);
        // kernel.cb 0x2c84 calls teardown 0x6098 before the next scene;
        // its 568(0) at 0x6338 prevents room fog leaking into the exterior.
        camera_.end_scene();
        auto next=scene_result;
        const bool loaded_game=active_scene_==101;
        // Data Load's UI process can finish with its fade/display suspension
        // still set. That transition state belongs to scene 101 and must not
        // cover the restored gameplay scene (software or hardware renderer).
        // New Game's tutorial also enters without a negative reveal fade.
        // Release the title's blackout before its own CF/scene setup begins.
        if(loaded_game||(active_scene_==1004&&next==1000))interface_.resume_scene();
        // Data Load creates __gamecontext inside its scene allocation scope.
        // The original kernel restores it at 0x2cc4 before operation 119 at
        // 0x2d40 releases the scene. Other scenes restore their allocation
        // checkpoint immediately on return from the scene script.
        if(loaded_game)process_->restore_context("__gamecontext");
        restore_scene_resources();
        // kernel.cb 2c94..2d90 restores context after Data Load / Data Save.
        // The original scene entry then restores its actors from these flags.
        if(active_scene_==100||active_scene_==101){
            const bool loading=active_scene_==101;
            pending_floating_load_=loading&&(next==94||next==37);
            if(!loading)process_->restore_context("__gamecontext");
            // kernel.cb 0x2ce4..0x2cf4 (Data Load) and 0x2dcc..0x2ddc
            // (Data Save) call 164(1). The storage script disables pause;
            // that permission must not leak into the resumed game.
            pause_.enable(true);
            active_scene_=0;
            // kernel.cb 0xcc..0xdc sets the save-resume flag before capturing
            // a Data Save context. Older recomp saves omitted that kernel
            // step, so also restore the invariant when loading one of them.
            process_->flags()[2000]=true;
            // kernel.cb 2d94..2dc8: returning from Data Save with the ending
            // scene redirects to the title, not another run of the ending.
            // Keep scene 99 in the persisted context for Complete Data/Bonus.
            if(!loading&&next==99){next=1004;release_save_return_location();}
            if(!loading&&save_return_location_){
                restoring_save_location_=true;
                // Scene 100 leaves Rio seated. Restore the captured room pose
                // in this same transition frame so the save menu cannot expose
                // a one-frame seated pose before the room worker resumes.
                objects_.restore_actor_location(save_return_location_->first,save_return_location_->second,true,save_return_idle_);
            }
            if(loading&&next==99){
                // kernel.cb 2cf8..2d3c: Complete Data starts a new tale after
                // the authored persistent-flag / health / ammo reset helper.
                auto kernel=std::make_shared<script::Program>("kernel.cb",assets_.read(installed::program("kernel.cb")));
                auto bindings=script::memory_bindings();bindings.merge(inventory_.bindings());
                script::State state;state.flags=process_->flags();
                script::Runtime reset(kernel,state,bindings,0x3650/4);reset.run_until(0x3950/4);
                next=13;
            }
        }
        if(next==1004&&!frontend_menu()){title_requested_=true;return;}
        if(next>0){
            if(loaded_game){
                // Permanent kernel resources (2ee4..2f88), normally prepared
                // before the title: shared motion bank, weapon and muzzle art.
                resources_.load("teru/riomot.a");resources_.load("teru/obj/sk118.a");resources_.load("teru/gunfire.a");
                // kernel.cb fc..334 selects Rio's current model/animation bank
                // from the restored story and completion flags before entry.
                auto kernel=std::make_shared<script::Program>("kernel.cb",assets_.read(installed::program("kernel.cb")));
                auto bindings=objects_.bindings();bindings.merge(script::resource_bindings(resources_));
                script::State state;state.flags=process_->flags();state.integers[1]=next;
                script::Runtime model(kernel,state,bindings,0xfc/4);model.run_until(0x334/4);
            }
            // kernel.cb a8..f4 captures the scene before loading the save UI.
            if(next==100){
                // The original kernel sets this before operation 1116 writes
                // __gamecontext. Room entry uses it to select the save-point
                // idle/load setup instead of replaying an entrance route.
                process_->flags()[2000]=true;
                process_->capture_context("__gamecontext");
                const auto& motion=objects_.actor_motion();
                save_return_location_=std::pair{motion.position,motion.rotation};
                save_return_idle_=(active_scene_==94||active_scene_==37)?92u:0u;
            }
            auto number=std::to_string(next);while(number.size()<3)number='0'+number;
            pending_route_entry_heading_=false;
            pending_post_brain_exit_arrival_=false;
            // Once the brain event is complete, Exit is an ordinary return,
            // not the uninterruptible introduction to the corridor. Preserve
            // its doorway and camera event; finish swimming once clear of
            // the doorway. Pre-brain traversal is unchanged.
            if(active_scene_==37&&next==94&&process_->flags()[800]&&
                objects_.scene_entry()==std::array<std::int32_t,4>{38,1,1,7076}){
                process_->flags()[431]=false;
                pending_route_entry_heading_=true;
                pending_post_brain_exit_arrival_=true;
            }
            // After the brain unlock, Hatch 3 returns through its side branch
            // (094.rs route 46, reversed), not directly onto the main passage
            // (route 16). Keep the authored camera/route-resume event 7073:
            // it advances out of the hatch and rejoins the corridor naturally.
            // Match the complete transfer tuple so Exit's route 38 and the
            // pre-brain locked response remain untouched.
            if(active_scene_==37&&next==94&&process_->flags()[800]&&
                objects_.scene_entry()==std::array<std::int32_t,4>{16,0,1,7073}){
                objects_.scene_entry({46,1,1,7073});pending_route_entry_heading_=true;
            }
            begin_scene_resources();
            resources_.load("teru/cs/cs"+number+".a");
              objects_.begin_route_context();
              if(active_scene_==94&&next==37){
                  const auto entry=objects_.scene_entry();
                  if(entry==std::array<std::int32_t,4>{5,0,0,-1})objects_.lab_entry_route(18);
                  else if(entry==std::array<std::int32_t,4>{1,0,0,-1})objects_.lab_entry_route(1);
              }
            scene_group_=process_->start_scene("entry_"+number+".cb");
            if(scene_group_<0)throw std::runtime_error("No worker available for next authored scene");
            active_scene_=next;
        }
    }
}
}
