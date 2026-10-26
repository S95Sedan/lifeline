#include "media/sequence_playback.hpp"
#include "media/audio_output.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
namespace lifeline {
struct SequencePlayback::Impl {
    Music music;bool silent=false,paused=false,stopped=false;
    std::uint64_t fade_samples=0;unsigned fade_ticks=0;
    AudioStream stream;
    Impl(const AssetDirectory& assets,const std::string& path,bool quiet):music(assets,path),silent(quiet){music.volume(127);}
    void render(std::span<float> samples){
        music.render(samples);
        apply_audio_output(samples);
        if(fade_ticks)for(std::size_t i=0;i<samples.size();i+=2){
            const auto tick=fade_samples++/800+1;
            const float gain=tick>=fade_ticks?0.f:float((127*(fade_ticks-tick))/fade_ticks)/127.f;
            samples[i]*=gain;samples[i+1]*=gain;
            if(tick>=fade_ticks){music.stop();stopped=true;}
        }
    }
    void advance(){
        if(paused || stopped || !music.active())return;
        if(silent){std::array<float,1600> samples{};render(samples);return;}
        const bool starting=!stream;
        if(starting){SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};stream=open_audio_stream(AudioBus::music,spec);}
        const auto queued=SDL_GetAudioStreamQueued(stream.get());if(queued<0)throw std::runtime_error(SDL_GetError());
        // Retain 300 ms through expensive software-rendered effect views.
        constexpr int capacity=28800*sizeof(float);
        if(queued<capacity){std::array<float,28800> samples{};const auto count=unsigned(capacity-queued)/sizeof(float)/2*2;render(std::span(samples).first(count));if(!SDL_PutAudioStreamData(stream.get(),samples.data(),int(count*sizeof(float))))throw std::runtime_error(SDL_GetError());}
        if(starting&&!SDL_ResumeAudioStreamDevice(stream.get()))throw std::runtime_error(SDL_GetError());
    }
};
SequencePlayback::SequencePlayback(const AssetDirectory& assets,const std::string& path,bool silent):impl_(std::make_unique<Impl>(assets,path,silent)){}
SequencePlayback::~SequencePlayback()=default;
void SequencePlayback::advance(){impl_->advance();}
void SequencePlayback::pause(bool value){if(impl_->stream){const bool ok=value?SDL_PauseAudioStreamDevice(impl_->stream.get()):SDL_ResumeAudioStreamDevice(impl_->stream.get());if(!ok)throw std::runtime_error(SDL_GetError());}impl_->paused=value;}
void SequencePlayback::fade(unsigned ticks){if(!ticks){impl_->music.stop();impl_->stopped=true;impl_->stream.reset();}else{impl_->fade_ticks=ticks;impl_->fade_samples=0;}}
bool SequencePlayback::active()const{return (!impl_->stopped && impl_->music.active()) || (impl_->stream && SDL_GetAudioStreamQueued(impl_->stream.get())>0);}
bool SequencePlayback::paused()const{return impl_->paused;}
}
