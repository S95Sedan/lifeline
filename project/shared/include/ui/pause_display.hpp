#pragma once
#include "assets/asset_directory.hpp"
#include "ui/pause.hpp"
#include <memory>
struct SDL_Renderer;
namespace lifeline {
class PauseDisplay {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    PauseDisplay(const AssetDirectory&,SDL_Renderer*);
    ~PauseDisplay();
    void render(const Pause&);
};
}
