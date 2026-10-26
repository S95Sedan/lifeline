#pragma once
#include "assets/asset_directory.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <optional>
#include <array>
namespace lifeline {
enum class MicrophoneDisplay { hidden, idle, cannot_talk, talk_now, disconnected };
class GameHud {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    GameHud(const AssetDirectory&,SDL_Renderer*);
    ~GameHud();
    void microphone(MicrophoneDisplay);
    void microphone_assets(bool available);
    void health(std::optional<int> value);
    void spectrum(const std::array<float,16>& decibels);
    void score(bool visible,std::optional<int> value);
    void combat_layout(bool enabled);
    void vertical_offset(float);
    void advance();
    void render();
};
}
