#include "platform/audio_devices.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <set>

namespace lifeline {
namespace {
struct State {
    std::mutex mutex;
    AudioDeviceSettings settings;
    std::map<SDL_AudioStream*,SDL_AudioDeviceID> outputs;
    SDL_AudioDeviceID playback=SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,recording=SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    std::uint64_t recording_revision=1;
    bool recording_resolved=false;
    std::string recording_name;
    std::string error;
};
State& state(){static State value;return value;}
struct Catalog {std::mutex mutex;std::map<SDL_AudioDeviceID,std::string> ambiguous;std::uint64_t serial=0;};
Catalog& catalog(){static Catalog value;return value;}
void checked(bool success){if(!success)throw std::runtime_error(std::string("Audio device: ")+SDL_GetError());}
void initialize(){if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO))checked(SDL_InitSubSystem(SDL_INIT_AUDIO));}
struct Device {
    SDL_AudioDeviceID id=0;
    explicit Device(SDL_AudioDeviceID value=0):id(value){}
    ~Device(){if(id)SDL_CloseAudioDevice(id);}
    Device(const Device&)=delete;Device& operator=(const Device&)=delete;
};
SDL_AudioDeviceID resolve(const std::vector<AudioDeviceChoice>& choices,const std::string& key,bool recording){
    for(const auto& choice:choices)if(choice.key==key)return choice.id;
    return recording?SDL_AUDIO_DEVICE_DEFAULT_RECORDING:SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
}
std::string key_name(const std::string& key){
    if(!valid_audio_device_key(key)||key.empty())return "Unknown device";
    const auto begin=key.find(':',7)+1,end=key.find(':',begin);std::string name;
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=begin;i<end;i+=2)name+=char(digit(key[i])*16+digit(key[i+1]));
    return name;
}
// Called with the registry locked: destruction and migration cannot race.
void migrate(State& s,SDL_AudioDeviceID target){
    if(s.outputs.empty()){Device probe(SDL_OpenAudioDevice(target,nullptr));checked(probe.id!=0);return;}
    struct Move {SDL_AudioStream* stream;SDL_AudioDeviceID old;std::unique_ptr<Device> next;bool paused;};
    std::vector<Move> moves;moves.reserve(s.outputs.size());
    for(const auto& [stream,old]:s.outputs){
        auto next=std::make_unique<Device>(SDL_OpenAudioDevice(target,nullptr));checked(next->id!=0);checked(SDL_PauseAudioDevice(next->id));
        moves.push_back({stream,old,std::move(next),SDL_AudioDevicePaused(old)});
    }
    try{
        for(const auto& move:moves)checked(SDL_PauseAudioDevice(move.old));
        for(const auto& move:moves){SDL_UnbindAudioStream(move.stream);checked(SDL_BindAudioStream(move.next->id,move.stream));}
        for(const auto& move:moves)if(!move.paused)checked(SDL_ResumeAudioDevice(move.next->id));
    }catch(...){
        for(const auto& move:moves){SDL_PauseAudioDevice(move.next->id);SDL_UnbindAudioStream(move.stream);SDL_BindAudioStream(move.old,move.stream);if(!move.paused)SDL_ResumeAudioDevice(move.old);}
        throw;
    }
    for(auto& move:moves){s.outputs.at(move.stream)=move.next->id;move.next->id=0;SDL_CloseAudioDevice(move.old);}
}
void refresh(State& s,bool force=false){
    const auto output=resolve(audio_device_choices(false),s.settings.playback,false);
    const auto input=resolve(audio_device_choices(true),s.settings.microphone,true);
    const auto* label=SDL_GetAudioDeviceName(input);const std::string input_name=label?label:"";
    if(force||input!=s.recording||input_name!=s.recording_name){s.recording=input;s.recording_name=input_name;++s.recording_revision;}
    s.recording_resolved=true;
    if(force||output!=s.playback){migrate(s,output);s.playback=output;}
}
}
std::string audio_device_key(const std::string& name,const std::string& driver){
    constexpr char hex[]="0123456789abcdef";
    const auto encode=[&](const std::string& value){std::string encoded;for(unsigned char c:value){encoded+=hex[c>>4];encoded+=hex[c&15];}return encoded;};
    return "device:"+encode(driver)+":"+encode(name)+":named";
}
bool valid_audio_device_key(const std::string& key){
    if(key.empty())return true;
    if(!key.starts_with("device:")||key.size()>4096)return false;
    const auto middle=key.find(':',7),end=middle==std::string::npos?middle:key.find(':',middle+1);
    if(end==std::string::npos||middle==7||end==middle+1||(middle-7)%2||(end-middle-1)%2)return false;
    const auto hex=[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');};
    for(std::size_t i=7;i<end;++i)if(i!=middle&&!hex(key[i]))return false;
    const auto suffix=key.substr(end+1);if(suffix=="named")return true;
    return suffix.starts_with("session-")&&suffix.size()>8&&std::all_of(suffix.begin()+8,suffix.end(),[](char c){return c>='0'&&c<='9';});
}
std::vector<AudioDeviceChoice> audio_device_choices(bool recording){
    initialize();const auto fallback=recording?SDL_AUDIO_DEVICE_DEFAULT_RECORDING:SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    const auto* name=SDL_GetAudioDeviceName(fallback);
    std::vector<AudioDeviceChoice> result{{{},name?"System default ("+std::string(name)+")":"System default (unavailable)",fallback}};
    int count=0;auto* devices=recording?SDL_GetAudioRecordingDevices(&count):SDL_GetAudioPlaybackDevices(&count);
    std::unique_ptr<SDL_AudioDeviceID,decltype(&SDL_free)> owner(devices,SDL_free);
    if(!devices)throw std::runtime_error(std::string("Audio device enumeration: ")+SDL_GetError());
    std::map<std::string,unsigned> totals,names;std::set<SDL_AudioDeviceID> present;
    for(int i=0;i<count;++i){present.insert(devices[i]);if(const auto* label=SDL_GetAudioDeviceName(devices[i]))++totals[label];}
    auto& cache=catalog();std::scoped_lock lock(cache.mutex);
    // Removed devices must receive new session tokens on reconnection.
    std::erase_if(cache.ambiguous,[&](const auto& item){return SDL_IsAudioDevicePlayback(item.first)!=recording&&!present.contains(item.first);});
    for(int i=0;i<count;++i)if(const auto* label=SDL_GetAudioDeviceName(devices[i])){
        const auto occurrence=names[label]++;auto key=audio_device_key(label,SDL_GetCurrentAudioDriver());
        if(totals[label]>1||cache.ambiguous.contains(devices[i])){
            auto& token=cache.ambiguous[devices[i]];if(token.empty())token="session-"+std::to_string(SDL_GetTicksNS())+std::to_string(++cache.serial);
            key.replace(key.rfind(':')+1,std::string::npos,token);
        }
        result.push_back({std::move(key),std::string(label)+(totals[label]>1?" ("+std::to_string(occurrence+1)+")":""),devices[i]});
    }
    return result;
}
std::string audio_device_label(const std::vector<AudioDeviceChoice>& choices,const std::string& key){
    for(const auto& choice:choices)if(choice.key==key)return choice.label;
    const auto active=choices.empty()?"System default unavailable":choices.front().label;
    return key_name(key)+" (unavailable; "+active+")";
}
AudioDeviceSettings audio_device_settings(){auto& s=state();std::scoped_lock lock(s.mutex);return s.settings;}
void set_audio_device_settings(const AudioDeviceSettings& settings){
    if(settings.microphone_gain>200||!valid_audio_device_key(settings.playback)||!valid_audio_device_key(settings.microphone))throw std::runtime_error("Invalid audio device settings");
    auto& s=state();std::scoped_lock lock(s.mutex);if(settings==s.settings)return;
    const auto old=s.settings;s.settings=settings;
    try{if(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO){
        if(old.microphone!=settings.microphone){const SDL_AudioSpec spec{SDL_AUDIO_F32,1,16000};
            const auto target=resolve(audio_device_choices(true),settings.microphone,true);
            std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> probe(SDL_OpenAudioDeviceStream(target,&spec,nullptr,nullptr),SDL_DestroyAudioStream);checked(bool(probe));
        }
        refresh(s);
    }}
    catch(...){s.settings=old;s.recording_resolved=false;throw;}
    if(old.microphone!=settings.microphone||old.microphone_gain!=settings.microphone_gain)++s.recording_revision;
}
void audio_device_event(const SDL_Event& event){
    if(event.type!=SDL_EVENT_AUDIO_DEVICE_ADDED&&event.type!=SDL_EVENT_AUDIO_DEVICE_REMOVED&&event.type!=SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED)return;
    // SDL also emits removals for our closed logical devices.
    if(event.type!=SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED&&!SDL_IsAudioDevicePhysical(event.adevice.which))return;
    auto& s=state();std::scoped_lock lock(s.mutex);
    // SDL already converts a format change in-place. Reopening a healthy
    // input for its own initial format notification causes an open/event loop
    // on some backends. A changed default endpoint is detected by refresh's
    // target/name comparison and still invalidates the utterance.
    if(event.adevice.recording&&event.type!=SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED){++s.recording_revision;s.recording_resolved=false;}
    try{refresh(s);}
    catch(const std::exception& error){s.error=error.what();}
}
std::string take_audio_device_error(){auto& s=state();std::scoped_lock lock(s.mutex);auto error=std::move(s.error);s.error.clear();return error;}
RecordingDeviceSelection recording_device_selection(){
    initialize();auto& s=state();std::scoped_lock lock(s.mutex);
    if(!s.recording_resolved){s.recording=resolve(audio_device_choices(true),s.settings.microphone,true);const auto* label=SDL_GetAudioDeviceName(s.recording);s.recording_name=label?label:"";s.recording_resolved=true;}
    return {s.recording,s.recording_revision,s.settings.microphone_gain};
}
SDL_AudioStream* create_playback_stream(const SDL_AudioSpec& spec){
    initialize();auto& s=state();std::scoped_lock lock(s.mutex);
    const auto target=resolve(audio_device_choices(false),s.settings.playback,false);
    // Resolve a reconnection before publishing a new stream, even when its
    // hotplug event has not yet reached the frontend.
    if(target!=s.playback&&!s.outputs.empty()){migrate(s,target);s.playback=target;}
    Device device(SDL_OpenAudioDevice(target,&spec));checked(device.id!=0);checked(SDL_PauseAudioDevice(device.id));
    std::unique_ptr<SDL_AudioStream,decltype(&SDL_DestroyAudioStream)> stream(SDL_CreateAudioStream(&spec,nullptr),SDL_DestroyAudioStream);checked(bool(stream));checked(SDL_BindAudioStream(device.id,stream.get()));
    s.outputs.emplace(stream.get(),device.id);s.playback=target;device.id=0;return stream.release();
}
void destroy_playback_stream(SDL_AudioStream* stream)noexcept{
    if(!stream)return;auto& s=state();std::scoped_lock lock(s.mutex);
    const auto found=s.outputs.find(stream);const auto device=found==s.outputs.end()?0:found->second;
    if(found!=s.outputs.end())s.outputs.erase(found);
    SDL_DestroyAudioStream(stream);if(device)SDL_CloseAudioDevice(device);
}
std::int16_t microphone_pcm(float sample,unsigned gain){
    const double value=std::isfinite(sample)?std::clamp(double(sample)*(gain/100.0),-1.0,1.0):0;
    return static_cast<std::int16_t>(std::clamp(value*32768,-32768.0,32767.0));
}
}
