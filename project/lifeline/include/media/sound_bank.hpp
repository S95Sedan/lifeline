#pragma once
#include "assets/asset_directory.hpp"
#include <memory>
#include <span>
#include <cstdint>
namespace lifeline {
// Installed PCM and named sequence records; no disc access or console driver.
class SoundBank {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    SoundBank(const AssetDirectory&,const std::string& bank);
    ~SoundBank();
    std::uint64_t play(unsigned group,unsigned sequence,float left=1,float right=1);
    void stop(std::uint64_t instance,bool release=true);
    void stop_all(bool release=true);
    void render(std::span<float> stereo); // 48000 Hz, replaces output
    bool active()const;
    bool active(std::uint64_t instance)const;
    bool active(unsigned group,unsigned program,unsigned key)const;
    bool contains(unsigned group,unsigned sequence)const;
    unsigned sequence_count()const;
};
// Main-thread owner of a bounded SDL output queue. The callback never loads data.
class SoundPlayback {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SoundPlayback(const AssetDirectory&,const std::string& bank,bool silent=false);
    ~SoundPlayback();
    std::uint64_t play(unsigned group,unsigned sequence);
    void stop(std::uint64_t instance,bool release=true);
    void stop_all(bool release=true);
    bool active()const;
    bool cue_pending(std::uint64_t instance)const;
    bool active(unsigned group,unsigned program,unsigned key)const;
    bool contains(unsigned group,unsigned sequence)const;
    void advance();
    void pause(bool);
    void reverb_preset(unsigned);
    void reverb_depth(float);
};
}
