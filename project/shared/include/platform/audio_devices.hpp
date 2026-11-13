#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lifeline {
struct AudioDeviceSettings {
    // Empty means the OS default. Named selections use a persistent name key,
    // never SDL's process-local numeric device IDs.
    std::string playback,microphone;
    unsigned microphone_gain=100;
    bool operator==(const AudioDeviceSettings&)const=default;
};
struct AudioDeviceChoice {std::string key,label;SDL_AudioDeviceID id=0;};
std::string audio_device_key(const std::string& name,const std::string& driver);
bool valid_audio_device_key(const std::string&);
std::vector<AudioDeviceChoice> audio_device_choices(bool recording);
std::string audio_device_label(const std::vector<AudioDeviceChoice>&,const std::string& key);
AudioDeviceSettings audio_device_settings();
// Keeps existing stream queues/gains and independent paused states intact.
void set_audio_device_settings(const AudioDeviceSettings&);
void audio_device_event(const SDL_Event&);
std::string take_audio_device_error();
struct RecordingDeviceSelection {SDL_AudioDeviceID id;std::uint64_t revision;unsigned gain;};
RecordingDeviceSelection recording_device_selection();
// Playback producers use explicit devices so their streams can be rebound.
// Every returned stream owns a separate, initially paused logical device.
SDL_AudioStream* create_playback_stream(const SDL_AudioSpec&);
void destroy_playback_stream(SDL_AudioStream*)noexcept;
// One conversion point for microphone PCM, meter, preroll and ASR events.
std::int16_t microphone_pcm(float sample,unsigned gain);
}
