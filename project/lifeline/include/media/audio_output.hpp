#pragma once
#include <atomic>
#include <span>
#include "media/audio_settings.hpp"
#include "platform/audio_devices.hpp"
#include <SDL3/SDL_audio.h>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
namespace lifeline {
// Shared native equivalent of the sound driver's stereo/mono output switch.
inline std::atomic<bool> mono_output{false};
inline void apply_audio_output(std::span<float> samples){
    if(mono_output.load(std::memory_order_relaxed))for(std::size_t i=0;i+1<samples.size();i+=2)samples[i]=samples[i+1]=(samples[i]+samples[i+1])*.5f;
}
namespace audio_output_detail {
struct Gain {AudioBus bus;float authored;};
struct Outputs {
    std::mutex mutex;
    AudioVolumes volumes;
    std::map<SDL_AudioStream*,Gain> streams;
};
inline Outputs& outputs(){static Outputs value;return value;}
inline float gain(const AudioVolumes& volumes,Gain stream){
    const auto percent=stream.bus==AudioBus::fmv?volumes.fmv:stream.bus==AudioBus::dialogue?volumes.dialogue:volumes.music;
    return stream.authored*(percent/100.f);
}
}
inline AudioVolumes audio_volumes(){auto& state=audio_output_detail::outputs();std::scoped_lock lock(state.mutex);return state.volumes;}
inline void set_audio_volumes(AudioVolumes volumes){
    if(volumes.fmv>100||volumes.dialogue>100||volumes.music>100)throw std::runtime_error("Audio volumes must be between 0 and 100");
    auto& state=audio_output_detail::outputs();std::scoped_lock lock(state.mutex);
    if(volumes==state.volumes)return;
    // Stream gain applies as queued samples are consumed, including long
    // dialogue queues. Never rescale the queued PCM or restart its clock.
    for(const auto& [stream,gain]:state.streams)if(!SDL_SetAudioStreamGain(stream,audio_output_detail::gain(volumes,gain))){
        const std::string error=SDL_GetError();
        for(const auto& [output,previous]:state.streams)SDL_SetAudioStreamGain(output,audio_output_detail::gain(state.volumes,previous));
        throw std::runtime_error("Audio volume: "+error);
    }
    state.volumes=volumes;
}
struct AudioStreamDeleter {
    void operator()(SDL_AudioStream* stream)const noexcept{
        if(!stream)return;
        auto& state=audio_output_detail::outputs();
        {std::scoped_lock lock(state.mutex);state.streams.erase(stream);}
        destroy_playback_stream(stream);
    }
};
using AudioStream=std::unique_ptr<SDL_AudioStream,AudioStreamDeleter>;
inline AudioStream open_output_stream(const SDL_AudioSpec& spec){return AudioStream(create_playback_stream(spec));}
inline AudioStream register_audio_stream(AudioStream stream,AudioBus bus,float authored_gain=1.f){
    if(!std::isfinite(authored_gain)||authored_gain<0)throw std::runtime_error("Invalid authored audio gain");
    if(!stream)throw std::runtime_error(SDL_GetError());
    auto& state=audio_output_detail::outputs();std::scoped_lock lock(state.mutex);
    const audio_output_detail::Gain gain{bus,authored_gain};
    if(!SDL_SetAudioStreamGain(stream.get(),audio_output_detail::gain(state.volumes,gain)))throw std::runtime_error(SDL_GetError());
    state.streams.insert_or_assign(stream.get(),gain);return stream;
}
inline AudioStream open_audio_stream(AudioBus bus,const SDL_AudioSpec& spec,float authored_gain=1.f){
    return register_audio_stream(open_output_stream(spec),bus,authored_gain);
}
}
