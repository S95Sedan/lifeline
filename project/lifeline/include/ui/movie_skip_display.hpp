#pragma once
#include "assets/asset_directory.hpp"
#include <memory>
struct SDL_Renderer;
namespace lifeline {
class MovieSkipDisplay {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    MovieSkipDisplay(const AssetDirectory&,SDL_Renderer*);
    ~MovieSkipDisplay();
    void render(float progress);
};
}
