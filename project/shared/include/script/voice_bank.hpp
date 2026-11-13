#pragma once
#include "script/runtime.hpp"
#include "assets/resources.hpp"
#include "game/animation.hpp"
#include "media/audio_output.hpp"
#include <SDL3/SDL.h>
#include <array>

namespace lifeline::script {
// Selected installed recordings, retaining their authored numeric cue IDs.
class VoiceBank {
    Resources& resources_;
    std::string name_;
    std::map<unsigned,std::shared_ptr<const Bytes>> recordings_;
    struct Playback {
        AudioStream stream;
        std::uint64_t silent_samples=0;
        unsigned source_bytes=0;
        bool active=false;
    };
    std::array<Playback,4> channels_;
    std::optional<unsigned> current_;
    std::optional<unsigned> face_channel_;
    unsigned voice_=0;
    bool silent_=false;
    struct FaceKey {unsigned base=0,pose=0,ticks=0;float weight=0;};
    std::vector<FaceKey> face_keys_;
    unsigned face_blend_=0,face_key_=0,face_tick_=0,face_end_=0;
    std::optional<FacialExpression> face_;
    bool select_face(std::span<const std::uint8_t> wav);
    void advance_face();
    bool playing(const Playback&)const;
    void release(Playback&);
public:
#ifdef LIFELINE_DEV_SCENE_PROBE
    void debug_enable_audio(){stop();silent_=false;}
    void debug_set_face_tick(unsigned tick){face_tick_=tick;}
#endif
    explicit VoiceBank(Resources& resources,bool silent=false):resources_(resources),silent_(silent){}
    Bindings bindings();
    void select(const std::string& member);
    std::shared_ptr<const Bytes> recording(unsigned id)const;
    const std::string& name()const{return name_;}
    std::size_t size()const{return recordings_.size();}
    void play(unsigned);
    void stop();
    void reset();
    void advance();
    void pause(bool);
    unsigned active_channels()const;
    bool speaking()const;
    unsigned voice()const{return voice_;}
    const std::optional<FacialExpression>& facial_expression()const{return face_;}
};
}
