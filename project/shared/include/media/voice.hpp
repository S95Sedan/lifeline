#pragma once
#include "assets/asset_directory.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <memory>

namespace lifeline {
// Installed narration is mono PCM WAV at the original sample rate.
inline std::vector<std::int16_t> load_voice(const AssetDirectory& assets,const std::string& path) {
    const auto file=assets.resolve(path);
    SDL_AudioSpec spec{};Uint8* raw=nullptr;Uint32 size=0;
    if(!SDL_LoadWAV(file.string().c_str(),&spec,&raw,&size))throw std::runtime_error("Cannot load voice "+path+": "+SDL_GetError());
    std::unique_ptr<Uint8,decltype(&SDL_free)> bytes(raw,SDL_free);
    if(spec.freq!=13230 || spec.channels!=1 || spec.format!=SDL_AUDIO_S16 || !size || size%2)
        throw std::runtime_error("Invalid installed voice format: "+path);
    std::vector<std::int16_t> samples(size/2);std::memcpy(samples.data(),raw,size);return samples;
}
}
