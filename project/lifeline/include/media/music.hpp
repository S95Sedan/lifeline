#pragma once
#include "assets/asset_directory.hpp"
#include <cstdint>
#include <memory>
#include <span>
namespace lifeline {
// Native sequencer using installed PCM samples and explicit music records.
class Music {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Music(const AssetDirectory& assets,const std::string& sequence);
    ~Music();
    void render(std::span<float> interleaved_stereo); // 48000 Hz
    std::uint64_t loop_count() const;
    std::uint64_t loop_frames() const;
    unsigned event_count() const;
    unsigned sample_count() const;
    bool active()const;
    void volume(unsigned value);
    void stop();
};
}
