#pragma once
#include "assets/asset_directory.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <cstdint>
#include <string>

namespace lifeline {
class Cinematic {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Cinematic(const AssetDirectory&,SDL_Renderer*,const std::string& name,bool capture=false,unsigned frame=0,bool mono=false);
    ~Cinematic();
    bool advance();
    bool skip_section(); // True when another section remains playing.
    void render();
    unsigned current_frame() const;
    std::uint64_t playback_generation()const;
};
}
