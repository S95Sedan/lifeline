#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace lifeline {
// Settings-only input monitoring. Samples are measured and immediately
// discarded; they never reach speech recognition, a file or playback.
class MicrophoneMonitor {
    friend struct MicrophoneMonitorProbe;
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream_{nullptr,SDL_DestroyAudioStream};
    Uint64 revision_=0,last_update_=0,clip_until_=0;
    SDL_AudioDeviceID device_=0;
    bool attempted_=false;
    float peak_=0;
    unsigned level_=0;
    std::string error_;
public:
    void update(bool enabled,Uint64 now=SDL_GetTicks());
    void stop()noexcept;
    unsigned level()const{return level_;}
    bool clipping()const{return clip_until_!=0;}
    bool listening()const{return bool(stream_);}
    const std::string& error()const{return error_;}
};
}
