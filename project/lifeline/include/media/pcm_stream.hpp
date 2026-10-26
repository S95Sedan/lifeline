#pragma once
#include "assets/asset_directory.hpp"
#include "media/audio_playback.hpp"
#include <memory>
#include <span>
#include <optional>
namespace lifeline {
// Installed PCM playback. Source-byte limits are resolved before construction.
class PcmStream : public AudioPlayback {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    PcmStream(const AssetDirectory&,const std::string&,unsigned channels,unsigned rate,
              std::optional<std::uint64_t> frames,bool repeat,float gain,bool silent=false);
    ~PcmStream() override;
    void advance() override;
    void pause(bool) override;
    void fade(unsigned ticks) override;
    bool active()const override;
    bool paused()const override;
    std::uint64_t position()const;
    void render(std::span<float> stereo);
};
}
