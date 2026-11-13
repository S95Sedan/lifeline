#pragma once
#include "media/audio_playback.hpp"
#include "media/music.hpp"
namespace lifeline {
class SequencePlayback : public AudioPlayback {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SequencePlayback(const AssetDirectory&,const std::string&,bool silent=false);
    ~SequencePlayback() override;
    void advance() override;
    void pause(bool) override;
    void fade(unsigned ticks) override;
    bool active()const override;
    bool paused()const override;
};
}
