#pragma once
#include "script/runtime.hpp"
#include "media/movie_playback.hpp"
#include <array>
#include <cstdint>

namespace lifeline::script {
class Movies {
    AssetDirectory assets_;
    bool silent_,draw_=true,paused_=false;
    bool skip_next_=false,debug_completed_=false;
    std::uint64_t fullscreen_generation_=0,pending_skip_generation_=0;
    bool player_skipped_=false;
    struct Stream {
        unsigned state=0,frames=0,decoded=0,loop_frames=0,section=0;
        bool paused=false,loop=false,owned=false,skipping=false;
        int audio=-1;
        std::string name;
        std::unique_ptr<MoviePlayback> playback;
        std::array<int,6> properties{};
        std::array<AssetImage,2> buffers{{{"movie",0,0,{}},{"movie",0,0,{}}}};
    };
    std::array<Stream,17> streams_;
    mutable AssetImage scaled_{"fullscreen movie",0,0,{}};
    mutable unsigned scaled_frame_=0;
    std::function<int()> reserve_audio_;
    std::function<void(int)> release_audio_;
    void start(unsigned,const std::string&,bool loop);
    void stop(unsigned);
    void complete(unsigned,bool player_skip=false);
    unsigned handle(unsigned)const;
    int property(unsigned,int)const;
public:
    explicit Movies(const AssetDirectory& assets,bool silent=false):assets_(assets),silent_(silent){}
    ~Movies();
    void audio_channels(std::function<int()> reserve,std::function<void(int)> release){reserve_audio_=std::move(reserve);release_audio_=std::move(release);}
    Bindings bindings();
    void skip_next_cinematic(){skip_next_=true;} // Explicit debug entry only.
    bool debug_skip_completed()const{return debug_completed_;}
    // Identifies one primary section, including its loading state.
    // Zero means no primary movie; replacement sessions get distinct identities.
    std::uint64_t fullscreen_generation()const{return fullscreen_generation_;}
    // Drawing can be disabled during a longer in-engine pre-roll. Readiness
    // remains a separate condition for timing the hold.
    bool fullscreen_visible()const{return draw_&&streams_[0].state!=0;}
    bool fullscreen_skippable()const;
    unsigned fullscreen_frame()const; // One-based decoded picture, like opcode 812.
    bool request_skip_fullscreen(std::uint64_t expected_generation);
    void advance();
    void pause(bool);
    void close_audio_channel(int);
    void render(AssetImage&)const;
    const AssetImage& image(unsigned handle)const;
};
}
