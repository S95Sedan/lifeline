#pragma once
#include "assets/flash.hpp"
#include "platform/menu_input.hpp"

namespace lifeline {
class AssetDirectory;
// One native input owner for all five title choices; original scene transitions
// are committed through GameSession after the authored reveal has completed.
class TitleExit {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool active_=false,dialog_=false,accept_=false,requested_=false,committing_=false,back_=false;
    unsigned selection_=0;
    std::optional<unsigned> original_request_;
public:
    TitleExit(const AssetDirectory&,SDL_Renderer*);
    ~TitleExit();
    void active(bool,unsigned initial_selection=0);
    bool action(MenuAction);
    bool owns_input()const{return active_;}
    unsigned selection()const{return selection_;}
    bool quit_selected()const{return active_&&selection_==4;}
    bool dialog_open()const{return dialog_;}
    bool committing()const{return committing_;}
    bool back_requested()const{return back_;}
    std::optional<unsigned> original_request()const{return original_request_;}
    void committed(){original_request_.reset();back_=false;}
    bool requested()const{return requested_;}
    void render();
};
}
