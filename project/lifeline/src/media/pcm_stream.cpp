#include "media/pcm_stream.hpp"
#include "media/audio_output.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <bit>
#include <stdexcept>
namespace lifeline {
struct PcmStream::Impl {
    std::vector<std::int16_t> pcm;
    unsigned rate=0,channels=0;
    std::uint64_t frame=0,length=0,fade_frames=0,fade_position=0;
    bool repeat=false,silent=false,paused=false,ended=false;
    float gain=1;
    AudioStream stream;
    Impl(const AssetDirectory& assets,const std::string& path,unsigned count,unsigned frequency,std::optional<std::uint64_t> limit,bool looping,float volume,bool quiet)
        :rate(frequency),channels(count),repeat(looping),silent(quiet),gain(volume){
        SDL_AudioSpec spec{};Uint8* data=nullptr;Uint32 size=0;
        if(!SDL_LoadWAV(assets.resolve(path).string().c_str(),&spec,&data,&size))throw std::runtime_error(SDL_GetError());
        std::unique_ptr<Uint8,decltype(&SDL_free)> owned(data,SDL_free);
        if(count!=2 || !frequency || spec.channels!=int(count) || spec.freq!=int(frequency) || spec.format!=SDL_AUDIO_S16LE || !size || size%(2*count))throw std::runtime_error("Installed stream does not match authored audio format");
        if(!std::isfinite(volume) || volume<0 || volume>1)throw std::runtime_error("Invalid stream gain");
        length=size/(2*count);if(limit)length=std::min(length,*limit);ended=length==0;
        pcm.resize(std::size_t(length)*count);
        // The installed WAV is already S16LE. Avoid rebuilding millions of
        // unchanged samples when a transition starts a new background track.
        if constexpr(std::endian::native==std::endian::little){
            if(!pcm.empty())std::memcpy(pcm.data(),data,pcm.size()*sizeof(pcm[0]));
        }else for(std::size_t i=0;i<pcm.size();++i)pcm[i]=std::int16_t(std::uint16_t(data[2*i])|(std::uint16_t(data[2*i+1])<<8));
    }
    void render(std::span<float> out){
        if(out.size()%2)throw std::runtime_error("Stream output must be stereo");
        std::fill(out.begin(),out.end(),0.f);if(paused || ended)return;
        for(std::size_t i=0;i<out.size();i+=2){
            if(frame==length){if(repeat)frame=0;else{ended=true;break;}}
            float volume=gain;
            if(fade_frames){
                const auto tick_frames=std::max(1u,rate/60);const auto tick=fade_position/tick_frames+1;
                const auto ticks=fade_frames/tick_frames;
                if(tick>=ticks){ended=true;break;}
                volume=std::floor(gain*16383.f*float(ticks-tick)/float(ticks))/16383.f;++fade_position;
            }
            out[i]=pcm[frame*2]*(volume/32768.f);out[i+1]=pcm[frame*2+1]*(volume/32768.f);++frame;
        }
        if(frame==length && !repeat)ended=true;
        if(fade_frames && fade_position>=fade_frames)ended=true;
        apply_audio_output(out);
    }
    void advance(){
        if(paused || ended)return;
        if(silent){std::vector<float> block((rate/60)*2);render(block);return;}
        const bool starting=!stream;
        if(starting){
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,int(rate)};
            stream=open_audio_stream(AudioBus::music,spec);
            if(!stream)throw std::runtime_error(SDL_GetError());
        }
        const int queued=SDL_GetAudioStreamQueued(stream.get());if(queued<0)throw std::runtime_error(SDL_GetError());
        // Keep enough decoded audio queued to survive dense effect rendering.
        const unsigned capacity=(rate*3/10)*2*sizeof(float);
        if(unsigned(queued)<capacity){
            std::vector<float> block((capacity-unsigned(queued))/sizeof(float)/2*2);render(block);
            if(!SDL_PutAudioStreamData(stream.get(),block.data(),int(block.size()*sizeof(float))))throw std::runtime_error(SDL_GetError());
        }
        if(starting&&!SDL_ResumeAudioStreamDevice(stream.get()))throw std::runtime_error(SDL_GetError());
    }
};
PcmStream::PcmStream(const AssetDirectory& assets,const std::string& path,unsigned channels,unsigned rate,std::optional<std::uint64_t> frames,bool repeat,float gain,bool silent)
    :impl_(std::make_unique<Impl>(assets,path,channels,rate,frames,repeat,gain,silent)){}
PcmStream::~PcmStream()=default;
void PcmStream::advance(){impl_->advance();}
void PcmStream::pause(bool value){
    if(impl_->stream){const bool ok=value?SDL_PauseAudioStreamDevice(impl_->stream.get()):SDL_ResumeAudioStreamDevice(impl_->stream.get());if(!ok)throw std::runtime_error(SDL_GetError());}
    impl_->paused=value;
}
void PcmStream::fade(unsigned ticks){if(!ticks){impl_->ended=true;impl_->stream.reset();}else{impl_->fade_frames=std::max<std::uint64_t>(1,std::uint64_t(ticks)*impl_->rate/60);impl_->fade_position=0;}}
bool PcmStream::active()const{if(!impl_->ended)return true;return impl_->stream && SDL_GetAudioStreamQueued(impl_->stream.get())>0;}
bool PcmStream::paused()const{return impl_->paused;}
std::uint64_t PcmStream::position()const{return impl_->frame;}
void PcmStream::render(std::span<float> stereo){impl_->render(stereo);}
}
