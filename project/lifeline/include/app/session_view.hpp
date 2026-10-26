#pragma once
#include "assets/asset_directory.hpp"
#include <SDL3/SDL.h>
#include <memory>
namespace lifeline {
class GameSession;
class MenuInput;
class MovieSkipHold;
enum class MenuAction;
class SessionView {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SessionView(const AssetDirectory&,SDL_Renderer*,bool silent=false,bool load_game=false,bool title=false);
    SessionView(const AssetDirectory&,SDL_Renderer*,std::unique_ptr<GameSession>,bool silent=true);
    ~SessionView();
    void confirm();
    bool cancel();
    void event(const SDL_Event&,SDL_JoystickID selected_gamepad);
    void event(const SDL_Event&,MenuInput&);
    void action(MenuAction);
    std::string status() const;
    void render();
    void movie_skip_hold(std::shared_ptr<MovieSkipHold>);
    bool return_to_title()const;
    bool options_requested()const;
    bool settings_open()const;
    void finish_options();
};
}
