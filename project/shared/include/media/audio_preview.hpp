#pragma once
#include "assets/asset_directory.hpp"
#include "media/audio_settings.hpp"
#include <memory>

namespace lifeline {
class AudioPreview {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit AudioPreview(const AssetDirectory&);
    ~AudioPreview();
    void play(AudioBus);
    void update();
    void stop();
    bool playing()const noexcept;
    const std::string& error()const;
    std::string take_error();
};
}
