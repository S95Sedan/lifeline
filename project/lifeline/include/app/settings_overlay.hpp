#pragma once
#include "assets/asset_directory.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace lifeline {
class OptionsController;
// In-game settings own their input while the paused scene is still rendered.
class SettingsOverlay {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SettingsOverlay(const AssetDirectory&,SDL_Renderer*);
    ~SettingsOverlay();
    void event(const SDL_Event&);
    void update();
    void render();
    bool closed()const;
    const OptionsController& controller()const;
    static SDL_FRect rectangle(SDL_Renderer*);
};
}
