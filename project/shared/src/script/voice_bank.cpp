#include "script/voice_bank.hpp"
#include <charconv>
#include <cmath>
#include <bit>
#include <algorithm>

namespace lifeline::script {
bool VoiceBank::select_face(std::span<const std::uint8_t> wav){
    const auto word=[&](std::size_t p){if(p+4>wav.size())throw std::runtime_error("Truncated voice facial track");return unsigned(wav[p])|unsigned(wav[p+1])<<8|unsigned(wav[p+2])<<16|unsigned(wav[p+3])<<24;};
    for(std::size_t p=12;p+8<=wav.size();){
        const auto tag=word(p),size=word(p+4);p+=8;
        if(size>wav.size()-p)throw std::runtime_error("Truncated installed voice chunk");
        if(tag==0x6361666c){ // lfac: installed pose pairs, durations and weights.
            if(size<12 || word(p)!=1)throw std::runtime_error("Invalid voice facial version");
            const auto count=word(p+4),blend=word(p+8);
            if(count<2 || count>65536 || !blend || size!=12+count*16)throw std::runtime_error("Invalid voice facial extent");
            std::vector<FaceKey> keys;keys.reserve(count);
            for(unsigned i=0;i<count;++i){const auto q=p+12+i*16;FaceKey k{word(q),word(q+4),word(q+8),std::bit_cast<float>(word(q+12))};
                if(!k.ticks || !std::isfinite(k.weight))throw std::runtime_error("Invalid voice facial key");keys.push_back(k);}
            face_keys_=std::move(keys);face_blend_=blend;face_key_=face_tick_=0;face_end_=face_keys_[0].ticks;face_.reset();return true;
        }p+=size+(size&1);
    }
    // A cue without metadata does not replace service 8009's current track.
    return false;
}
void VoiceBank::advance_face(){
    if(face_keys_.empty())return;
    while(face_tick_>=face_end_ && face_key_+2<face_keys_.size()){
        ++face_key_;face_end_+=face_keys_[face_key_].ticks;
    }
    const auto& a=face_keys_[face_key_];const auto& b=face_keys_[face_key_+1];
    FacialExpression result;result.poses={a.base,a.pose,b.base,b.pose};
    result.first_weight=a.weight;result.second_weight=b.weight;
    if(face_tick_>=face_end_){
        // A consumed-audio tick can skip the entire final pair, including on
        // the first sample. Materialize it before holding the terminal pose.
        result.mix_weight=1;face_=result;return;
    }
    // 1ccd7c divides elapsed ticks by the shorter blend/key duration;
    // 1ccd80 is SQRT.S (0x460c0004), then the result is capped at one.
    result.mix_weight=std::min(1.f,std::sqrt(float(a.ticks-(face_end_-face_tick_))/float(std::min(face_blend_,a.ticks))));
    face_=result;++face_tick_;
}
void VoiceBank::pause(bool paused){for(auto& channel:channels_)if(channel.stream){const bool ok=paused?SDL_PauseAudioStreamDevice(channel.stream.get()):SDL_ResumeAudioStreamDevice(channel.stream.get());if(!ok)throw std::runtime_error(SDL_GetError());}}
bool VoiceBank::playing(const Playback& channel)const{
    if(!channel.active)return false;
    if(silent_)return channel.silent_samples!=0;
    if(!channel.stream)return false;const int queued=SDL_GetAudioStreamQueued(channel.stream.get());
    if(queued<0)throw std::runtime_error(SDL_GetError());return queued!=0;
}
bool VoiceBank::speaking()const{return current_ && playing(channels_[*current_]);}
unsigned VoiceBank::active_channels()const{unsigned count=0;for(const auto& channel:channels_)count+=playing(channel);return count;}
void VoiceBank::release(Playback& channel){
    // Reuse the four device streams between cues. Closing the last stream
    // can block the game thread while the audio device shuts down.
    if(channel.stream&&!SDL_ClearAudioStream(channel.stream.get()))throw std::runtime_error(SDL_GetError());
    channel.silent_samples=0;channel.source_bytes=0;channel.active=false;
}
void VoiceBank::stop(){
    face_channel_.reset();for(auto& channel:channels_)release(channel);
    if(face_){face_->poses[1]=face_->poses[0];face_->poses[3]=face_->poses[2];face_->first_weight=face_->second_weight=0;}
}
void VoiceBank::reset(){stop();for(auto& channel:channels_)channel=Playback{};current_.reset();recordings_.clear();name_.clear();}
void VoiceBank::advance(){
    // A render stall can cause several simulation updates in one burst. The
    // authored 60 Hz facial keys must follow consumed voice samples, not those
    // catch-up updates. Keep silent replays on their deterministic tick clock.
    if(!silent_ && face_channel_){
        const auto& channel=channels_[*face_channel_];
        if(channel.stream){
            const int queued=SDL_GetAudioStreamQueued(channel.stream.get());
            if(queued<0)throw std::runtime_error(SDL_GetError());
            const auto consumed=channel.source_bytes-std::min(channel.source_bytes,unsigned(queued));
            face_tick_=unsigned(std::uint64_t(consumed)*60/(13230*2));
        }
    }
    if(face_channel_&&playing(channels_[*face_channel_]))advance_face();
    else if(face_){
        // Stop advancing expired facial data. Facial metadata is not speaker
        // identity: the actor's authored enable/disable state decides whether
        // it applies, and cinematic clips retain their own facial animation.
        face_->poses[1]=face_->poses[0];face_->poses[3]=face_->poses[2];face_->first_weight=face_->second_weight=0;
    }
    for(unsigned slot=0;slot<channels_.size();++slot){auto& channel=channels_[slot];
        if(channel.silent_samples)channel.silent_samples=channel.silent_samples>13230?channel.silent_samples-13230:0;
        if(channel.active && !playing(channel)){if(face_channel_==slot)face_channel_.reset();release(channel);}
    }
}
void VoiceBank::play(unsigned id){
    // 162e80 searches four inactive slots. Saturation leaves the current cue
    // intact; a new cue does not implicitly stop the preceding one.
    unsigned slot=0;while(slot<channels_.size() && channels_[slot].active)++slot;
    if(slot==channels_.size())return;
    // 1630bc..1630d0 skips zero-length directory entries; 1632b4..1632c8
    // returns without starting playback when the cue search is exhausted.
    const auto found=recordings_.find(id);if(found==recordings_.end())return;
    const auto bytes=found->second;SDL_AudioSpec spec{};Uint8* raw=nullptr;Uint32 size=0;
    auto* input=SDL_IOFromConstMem(bytes->data(),bytes->size());
    if(!input || !SDL_LoadWAV_IO(input,true,&spec,&raw,&size))throw std::runtime_error(SDL_GetError());
    std::unique_ptr<Uint8,decltype(&SDL_free)> data(raw,SDL_free);
    if(spec.freq!=13230 || spec.channels!=1 || spec.format!=SDL_AUDIO_S16 || !size)throw std::runtime_error("Invalid installed voice format");
    if(select_face(*bytes))face_channel_=slot;
    channels_[slot].source_bytes=size;
    if(silent_){channels_[slot].silent_samples=std::uint64_t(size/2)*60;channels_[slot].active=true;current_=slot;voice_=id;return;}
    auto& stream=channels_[slot].stream;
    if(!stream)stream=open_audio_stream(AudioBus::dialogue,spec);
    if(!stream || !SDL_PutAudioStreamData(stream.get(),raw,int(size)) || !SDL_FlushAudioStream(stream.get()) || !SDL_ResumeAudioStreamDevice(stream.get())){
        const std::string error=SDL_GetError();if(stream)SDL_ClearAudioStream(stream.get());throw std::runtime_error(error);
    }
    channels_[slot].active=true;current_=slot;voice_=id;
}
void VoiceBank::select(const std::string& member){
    // Original 162be0..162c04 stores the nullable resource lookup and returns
    // on a miss. It neither reloads the archive nor stops queued playback.
    // entry_021 deliberately selects its unloaded bank during battle teardown.
    const auto* files=resources_.find_loaded_files(member);
    if(!files){recordings_.clear();name_.clear();return;}
    std::map<unsigned,std::shared_ptr<const Bytes>> selected;
    for(const auto& file:*files){
        if(file.conversion!="voice-wav-v1" || !file.bytes)throw std::runtime_error("Voice bank needs installed WAV conversion");
        const auto path=std::filesystem::path(file.path);const auto stem=path.stem().string();unsigned id=0;
        const auto parsed=std::from_chars(stem.data(),stem.data()+stem.size(),id);
        if(path.extension()!=".wav" || parsed.ec!=std::errc{} || parsed.ptr!=stem.data()+stem.size() || stem.empty() || !selected.emplace(id,file.bytes).second)throw std::runtime_error("Invalid installed voice identifier");
        const auto& data=*file.bytes;
        if(data.size()<44 || std::string(data.begin(),data.begin()+4)!="RIFF" || std::string(data.begin()+8,data.begin()+12)!="WAVE")throw std::runtime_error("Invalid installed voice recording");
    }
    if(selected.empty())throw std::runtime_error("Empty installed voice bank");
    name_=member;recordings_=std::move(selected);
}
std::shared_ptr<const Bytes> VoiceBank::recording(unsigned id)const{
    const auto found=recordings_.find(id);if(found==recordings_.end())throw std::runtime_error("Missing voice cue "+std::to_string(id)+" in "+name_);
    return found->second;
}
Bindings VoiceBank::bindings(){
    Bindings result;
    result.emplace(714,Binding{0,0,[this](const Invocation&){reset();return Reply{false,0.f};}});
    result.emplace(757,Binding{0,0,[this](const Invocation&){return Reply{false,speaking()?1.f:0.f};}});
    result.emplace(715,Binding{1,0,[this](const Invocation& c){
        const auto id=c.arguments[0];if(!std::isfinite(id) || id<0 || id>65535 || id!=std::floor(id))throw std::runtime_error("Invalid voice cue");
        play(unsigned(id));return Reply{false,0.f};
    }});
    result.emplace(716,Binding{0,0,[this](const Invocation&){stop();return Reply{false,0.f};}});
    result.emplace(717,Binding{0,0,[this](const Invocation&){
        return Reply{speaking(),0.f};
    }});
    result.emplace(713,Binding{1,0,[this](const Invocation& c){
        const auto address=c.arguments[0];
        if(!std::isfinite(address) || address<0 || address!=std::floor(address) || double(address)>=c.program.size())throw std::runtime_error("Invalid voice bank name address");
        select(c.program.string(std::uint32_t(address)));return Reply{false,0.f};
    }});
    return result;
}
}
