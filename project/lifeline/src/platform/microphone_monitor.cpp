#include "platform/microphone_monitor.hpp"
#include "platform/audio_devices.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace lifeline {
namespace {void checked(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}}
void MicrophoneMonitor::stop()noexcept{
    stream_.reset();attempted_=false;revision_=last_update_=clip_until_=0;device_=0;
    peak_=0;level_=0;error_.clear();
}
void MicrophoneMonitor::update(bool enabled,Uint64 now){
    if(!enabled){stop();return;}
    try{
        const auto selection=recording_device_selection();
        if(selection.revision!=revision_){
            // SDL converts format changes and migrates default logical devices
            // itself. Reopening on their format events can trigger another
            // format event indefinitely on real hardware. Gain changes also
            // need only reset the displayed peak, not the capture connection.
            if(selection.id!=device_||!stream_)stop();
            revision_=selection.revision;device_=selection.id;
            peak_=0;level_=0;clip_until_=0;last_update_=now;
        }
        if(!stream_){
            // A failed open retries on re-entry/focus regain or a device/gain
            // revision, rather than reopening the hardware every render frame.
            if(attempted_)return;
            attempted_=true;
            const SDL_AudioSpec spec{SDL_AUDIO_F32,1,16000};
            stream_.reset(SDL_OpenAudioDeviceStream(selection.id,&spec,nullptr,nullptr));checked(bool(stream_));
            checked(SDL_ResumeAudioStreamDevice(stream_.get()));last_update_=now;
        }
        std::array<float,1024> block{};float incoming=0;
        // Bound work after a stall; measured samples are immediately discarded.
        // Normal operation drains roughly one frame's worth of input.
        for(unsigned consumed=0;consumed<16000;){
            const auto available=SDL_GetAudioStreamAvailable(stream_.get());checked(available>=0);if(!available)break;
            const auto count=std::min({unsigned(available)/unsigned(sizeof(float)),unsigned(block.size()),16000-consumed});if(!count)break;
            const auto bytes=SDL_GetAudioStreamData(stream_.get(),block.data(),int(count*sizeof(float)));checked(bytes>=0);if(!bytes)break;
            consumed+=unsigned(bytes)/sizeof(float);
            for(int i=0;i<bytes/int(sizeof(float));++i){
                const auto sample=microphone_pcm(block[i],selection.gain);
                incoming=std::max(incoming,std::abs(float(sample))/32768.f);
                if(std::isfinite(block[i])&&std::abs(double(block[i])*selection.gain/100.0)>=.999)clip_until_=now+750;
            }
        }
        const auto elapsed=now>=last_update_?now-last_update_:0;last_update_=now;
        peak_=std::max(incoming,peak_*std::exp(-float(elapsed)/180.f));
        // A -60 to 0 dBFS display makes normal speaking levels visible.
        level_=unsigned(std::lround(std::clamp((20.f*std::log10(std::max(peak_,.001f))+60.f)/60.f,0.f,1.f)*100));
        if(now>=clip_until_)clip_until_=0;
    }catch(const std::exception& error){stream_.reset();peak_=0;level_=0;clip_until_=0;attempted_=true;error_=error.what();}
}
}
