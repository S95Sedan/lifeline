#pragma once
#include "speech/microphone_spectrum.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>
#include <deque>
#include "platform/bindings.hpp"
namespace lifeline {
struct MicrophoneUtteranceEvent {
    enum class Type {begin,audio,end,cancel};
    Type type=Type::audio;
    std::uint64_t utterance=0;
    std::vector<std::int16_t> samples;
};
// Native input measurement only. It does not classify words or award scores.
class MicrophoneInput {
    friend struct MicrophoneInputProbe;
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream_{nullptr,SDL_DestroyAudioStream};
    bool keyboard_=false,pad_=false,recording_=false,limited_=false,armed_=false,finishing_=false;
    Uint64 release_until_=0;
    std::vector<std::int16_t> preroll_;
    SDL_JoystickID pad_id_=0;
    BindingState bindings_;
    unsigned samples_=0;double energy_=0;
    std::string error_;
    std::vector<std::int16_t> pcm_;
    std::optional<std::vector<std::int16_t>> completed_;
    std::deque<MicrophoneUtteranceEvent> events_;
    std::uint64_t next_utterance_=1,active_utterance_=0;
    std::uint64_t device_revision_=0;
    unsigned gain_=100;
    bool device_release_=false;
    void synchronize_device();
    MicrophoneSpectrum spectrum_;
    std::int16_t meter_sample_=0;
    void synchronize();
    void stop();
    void finish();
public:
    ~MicrophoneInput();
    void prepare(); // Warm input and keep only the most recent 80 ms in memory.
    int device_state()const;
    // Readiness gates new presses, never releases or device/focus cleanup.
    void event(const SDL_Event&,SDL_JoystickID selected_gamepad,bool can_start=true);
    void update();
    void cancel();
    bool recording()const{return recording_;}
    std::int16_t meter_sample()const{return meter_sample_;}
    bool unavailable()const{return !error_.empty();}
    const std::array<float,16>& spectrum()const{return spectrum_.bands();}
    std::optional<std::vector<std::int16_t>> take_capture();
    std::optional<MicrophoneUtteranceEvent> take_event();
    void discard_events(){events_.clear();}
    std::string status() const;
};
}
