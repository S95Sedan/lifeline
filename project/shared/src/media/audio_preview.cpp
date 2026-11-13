#include "media/audio_preview.hpp"
#include "media/audio_output.hpp"
#include "media/movie_decoder.hpp"
#include "assets/installed_paths.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <condition_variable>
#include <thread>
#include <vector>
#include <algorithm>
#include <utility>

namespace lifeline {
namespace {
constexpr unsigned rate=48000;
using Clip=std::vector<float>;
void checked(bool ok){if(!ok)throw std::runtime_error(std::string("Audio preview: ")+SDL_GetError());}
Clip load(const AssetDirectory& assets,AudioBus bus,std::stop_token stop){
    Clip clip;
    if(bus==AudioBus::fmv){
        MovieDecoder decoder(assets.resolve(installed::movie("ts00",".mp4")),MovieDecodeMode::audio_only);
        constexpr unsigned count=rate*2*18;
        std::optional<std::size_t> start;
        while((start?clip.size()<*start+count:clip.size()<rate*2*10)&&!stop.stop_requested()){
            auto sample=decoder.read();if(!sample)break;
            if(sample->kind!=MovieSample::Kind::audio)continue;
            for(const auto sample:sample->pcm){
                if(!start&&std::abs(int(sample))>=64)start=clip.size()>rate/50?clip.size()-rate/50:0;
                clip.push_back(sample/32768.f);
            }
        }
        if(stop.stop_requested())return {};
        if(!start)throw std::runtime_error("Movie preview has no audible excerpt");
        const auto first=*start&~std::size_t(1);clip.erase(clip.begin(),clip.begin()+first);if(clip.size()>count)clip.resize(count);
    }else{
        const bool music=bus==AudioBus::music;
        const auto path=assets.resolve(music?"media/audio/streams/BGM12A.wav":"media/audio/voices/bank-013ai/4354.wav");
        SDL_AudioSpec spec{};Uint8* raw=nullptr;Uint32 size=0;
        checked(SDL_LoadWAV(path.string().c_str(),&spec,&raw,&size));
        std::unique_ptr<Uint8,decltype(&SDL_free)> pcm(raw,SDL_free);
        const auto frame_bytes=unsigned(spec.channels)*SDL_AUDIO_BYTESIZE(spec.format);
        const std::uint64_t offset=music?std::uint64_t(spec.freq)*20*frame_bytes:0;
        if(!frame_bytes||offset>=size)throw std::runtime_error("Audio preview excerpt is outside its source");
        const auto bytes=music?std::min<std::uint64_t>(size-offset,std::uint64_t(spec.freq)*20*frame_bytes):size;
        const SDL_AudioSpec output{SDL_AUDIO_F32,2,int(rate)};Uint8* converted=nullptr;int length=0;
        checked(SDL_ConvertAudioSamples(&spec,raw+offset,int(bytes),&output,&converted,&length));
        std::unique_ptr<Uint8,decltype(&SDL_free)> owned(converted,SDL_free);
        const auto* begin=reinterpret_cast<const float*>(converted);clip.assign(begin,begin+unsigned(length)/sizeof(float));
    }
    if(stop.stop_requested())return {};
    if(clip.empty())throw std::runtime_error("Audio preview has no samples");
    constexpr unsigned edge=rate/100;
    for(unsigned frame=0;frame<std::min<unsigned>(edge,unsigned(clip.size()/4));++frame)for(unsigned channel=0;channel<2;++channel){
        clip[frame*2+channel]*=float(frame)/edge;clip[clip.size()-2-frame*2+channel]*=float(frame)/edge;
    }
    return clip;
}
}
struct AudioPreview::Impl {
    AssetDirectory assets;
    std::mutex mutex;std::condition_variable_any changed;
    std::array<std::shared_ptr<const Clip>,3> clips;
    std::array<std::string,3> failures;
    std::optional<AudioBus> job,in_flight,desired,current;
    bool waiting=false,queued_mono=false;
    std::string error;
    AudioStream stream;
    std::jthread worker;
    explicit Impl(const AssetDirectory& data):assets(data){}
    ~Impl(){if(stream){SDL_PauseAudioStreamDevice(stream.get());SDL_ClearAudioStream(stream.get());}worker.request_stop();changed.notify_all();if(worker.joinable())worker.join();}
    void start_worker(){
        if(worker.joinable())return;
        worker=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()){
                AudioBus bus;
                {std::unique_lock lock(mutex);changed.wait(lock,stop,[&]{return job.has_value();});if(stop.stop_requested())return;bus=*job;job.reset();in_flight=bus;}
                try{
                    auto clip=std::make_shared<const Clip>(load(assets,bus,stop));
                    if(stop.stop_requested())return;
                    std::scoped_lock lock(mutex);clips[unsigned(bus)]=std::move(clip);in_flight.reset();
                }catch(const std::exception& e){std::scoped_lock lock(mutex);failures[unsigned(bus)]=e.what();in_flight.reset();}
            }
        });
    }
    bool playing()const{if(!stream)return false;const int queued=SDL_GetAudioStreamQueued(stream.get());checked(queued>=0);return queued>0;}
    void stop(){
        error.clear();
        if(!desired&&!current&&!waiting)return;
        desired.reset();current.reset();waiting=false;
        {std::scoped_lock lock(mutex);job.reset();}
        if(stream&&(!SDL_PauseAudioStreamDevice(stream.get())||!SDL_ClearAudioStream(stream.get())))stream.reset();
    }
    void play(AudioBus bus){
        if(unsigned(bus)>2)throw std::runtime_error("Invalid audio preview category");
        if(current==bus&&playing())return;
        if(desired==bus&&waiting)return;
        stop();error.clear();desired=bus;waiting=true;
        {std::scoped_lock lock(mutex);if(!clips[unsigned(bus)]&&in_flight!=bus){failures[unsigned(bus)].clear();job=bus;}}
        start_worker();changed.notify_all();
    }
    void update(){
        const bool mono=mono_output.load(std::memory_order_relaxed);
        if(current&&!waiting&&queued_mono!=mono&&playing()){const auto bus=*current;stop();play(bus);}
        if(!desired||!waiting)return;
        std::shared_ptr<const Clip> clip;
        {std::scoped_lock lock(mutex);clip=clips[unsigned(*desired)];error=failures[unsigned(*desired)];}
        if(!error.empty()){waiting=false;desired.reset();return;}
        if(!clip)return;
        if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO))checked(SDL_InitSubSystem(SDL_INIT_AUDIO));const SDL_AudioSpec spec{SDL_AUDIO_F32,2,int(rate)};
        if(stream)stream=register_audio_stream(std::move(stream),*desired);else stream=open_audio_stream(*desired,spec);
        if(mono){auto samples=*clip;apply_audio_output(samples);checked(SDL_PutAudioStreamData(stream.get(),samples.data(),int(samples.size()*sizeof(float))));}
        else checked(SDL_PutAudioStreamData(stream.get(),clip->data(),int(clip->size()*sizeof(float))));
        checked(SDL_FlushAudioStream(stream.get()));checked(SDL_ResumeAudioStreamDevice(stream.get()));current=desired;queued_mono=mono;waiting=false;
    }
    void failed(const std::exception& failure){stop();stream.reset();error=failure.what();}
};
AudioPreview::AudioPreview(const AssetDirectory& assets):impl_(std::make_unique<Impl>(assets)){}
AudioPreview::~AudioPreview()=default;
void AudioPreview::play(AudioBus bus){try{impl_->play(bus);}catch(const std::exception& e){impl_->failed(e);}}
void AudioPreview::update(){try{impl_->update();}catch(const std::exception& e){impl_->failed(e);}}
void AudioPreview::stop(){impl_->stop();}
bool AudioPreview::playing()const noexcept{try{return impl_->playing();}catch(...){return false;}}
const std::string& AudioPreview::error()const{return impl_->error;}
std::string AudioPreview::take_error(){return std::exchange(impl_->error,{});}
}
