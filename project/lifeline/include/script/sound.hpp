#pragma once
#include "script/runtime.hpp"
#include "assets/resources.hpp"
#include "media/sound_bank.hpp"
#include "media/pcm_stream.hpp"
#include <array>

namespace lifeline::script {
class Sound {
    AssetDirectory assets_;
    Resources& resources_;
    struct Instance {std::shared_ptr<SoundPlayback> bank;std::uint64_t cue=0;};
    std::array<std::shared_ptr<SoundPlayback>,6> banks_;
    std::array<std::string,6> bank_names_;
    std::array<std::optional<Instance>,8> channels_;
    std::optional<Instance> load_cue_;
    struct Stream {std::unique_ptr<AudioPlayback> audio;int handle=-1;bool sequenced=false;std::string bank;std::vector<ResourceFile> bank_files;int driver=-1;unsigned state=0;bool paused=false;};
    std::array<Stream,4> streams_;
    std::vector<Stream> fading_streams_;
    std::vector<Stream> direct_streams_;
    unsigned next_handle_=0;
    unsigned reverb_preset_=0;
    float reverb_depth_=0;
    std::array<bool,4> movie_channels_{};
    std::function<void(int)> close_movie_audio_;
    bool silent_;
    Bindings stream_bindings();
    int free_audio_handle(bool sequenced)const;
    int free_pcm_driver()const;
    const Stream* application_stream(int handle)const;
    std::unique_ptr<AudioPlayback> sequence(const std::vector<ResourceFile>& bank_files,const std::string& name);
public:
    void movie_audio_closer(std::function<void(int)> close){close_movie_audio_=std::move(close);}
    int reserve_movie_channel(){const int handle=free_audio_handle(false);if(handle>=0)movie_channels_[handle]=true;return handle;}
    void release_movie_channel(int handle){if(handle>=0)movie_channels_.at(unsigned(handle))=false;}
    unsigned play(unsigned bank,unsigned group,unsigned sequence,std::optional<unsigned> channel={},bool track_load=false);
    void stop_channel(unsigned channel,bool release);
    void stop_all(bool release);
    bool effects_active()const;
    bool effect_active(unsigned bank,unsigned group,unsigned program,unsigned key)const;
    Sound(const AssetDirectory& assets,Resources& resources,bool silent=false):assets_(assets),resources_(resources),silent_(silent){}
    Bindings bindings();
    void select(unsigned bank,const std::string& name);
    void unload(unsigned bank);
    void advance();
    void pause(bool);
};
}
