#include "platform/microphone.hpp"
#include "platform/audio_devices.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
namespace lifeline {
MicrophoneInput::~MicrophoneInput(){stop();}
void MicrophoneInput::synchronize_device(){
    const auto selection=recording_device_selection();
    if(selection.revision!=device_revision_){
        const auto held=bindings_;const bool keyboard=keyboard_,pad=pad_;const auto pad_id=pad_id_;
        events_.clear();cancel();
        device_release_=keyboard||pad;
        if(device_release_){bindings_=held;keyboard_=keyboard;pad_=pad;pad_id_=pad_id;}
        // Also invalidate recognizer work whose input already finished.
        if(events_.empty())events_.push_back({MicrophoneUtteranceEvent::Type::cancel,0,{}});
        stream_.reset();device_revision_=selection.revision;gain_=selection.gain;
    }
}
int MicrophoneInput::device_state()const{
    if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO)&&!SDL_InitSubSystem(SDL_INIT_AUDIO))return 0;
    int count=0;auto* devices=SDL_GetAudioRecordingDevices(&count);SDL_free(devices);
    return count>0?2:0;
}
void MicrophoneInput::stop(){if(stream_)SDL_PauseAudioStreamDevice(stream_.get());recording_=armed_=finishing_=false;preroll_.clear();spectrum_.reset();meter_sample_=0;}
void MicrophoneInput::finish(){
    const auto utterance=active_utterance_;stop();limited_=false;
    if(!pcm_.empty())completed_=std::move(pcm_);pcm_.clear();
    if(utterance)events_.push_back({MicrophoneUtteranceEvent::Type::end,utterance,{}});active_utterance_=0;
}
void MicrophoneInput::prepare(){
    try{synchronize_device();}catch(const std::exception& error){error_=error.what();return;}
    if(armed_ || completed_ || limited_)return;
    if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)){error_=SDL_GetError();return;}
    if(!stream_){const SDL_AudioSpec spec{SDL_AUDIO_F32,1,16000};stream_.reset(SDL_OpenAudioDeviceStream(recording_device_selection().id,&spec,nullptr,nullptr));}
    if(!stream_){error_=SDL_GetError();return;}
    if(!SDL_ClearAudioStream(stream_.get()) || !SDL_ResumeAudioStreamDevice(stream_.get())){error_=SDL_GetError();stop();return;}
    error_.clear();armed_=true;
}
void MicrophoneInput::synchronize(){
    if(!keyboard_ && !pad_){update();if(recording_){finishing_=true;release_until_=SDL_GetTicks()+160;}else if(limited_)finish();limited_=false;return;}
    if(finishing_){finishing_=false;return;}
    if(recording_ || limited_)return;
    error_.clear();samples_=0;energy_=0;pcm_.clear();completed_.reset();spectrum_.reset();
    prepare();if(!armed_)return;update();
    pcm_=std::move(preroll_);preroll_.clear();samples_=unsigned(pcm_.size());for(auto s:pcm_)energy_+=double(s)*s/(32768.0*32768.0);
    active_utterance_=next_utterance_++;
    events_.push_back({MicrophoneUtteranceEvent::Type::begin,active_utterance_,pcm_});
    recording_=true;
}
void MicrophoneInput::event(const SDL_Event& e,SDL_JoystickID selected,bool can_start){
    audio_device_event(e);
    try{synchronize_device();}catch(const std::exception& error){error_=error.what();return;}
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){cancel();return;}
    if((e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED)&&e.gdevice.which==pad_id_){bindings_.reset(1);pad_=false;pad_id_=0;if(!keyboard_)cancel();else synchronize();return;}
    for(const auto edge:bindings_.event(e,selected?selected:pad_id_))if(edge.action==InputAction::speak){
        if(edge.down&&!can_start){bindings_.release(InputAction::speak,edge.source);continue;}
        if(edge.source){pad_=edge.down;pad_id_=pad_?selected:0;}else keyboard_=edge.down;
        if(device_release_){if(!keyboard_&&!pad_)device_release_=false;continue;}
        synchronize();
    }
}
void MicrophoneInput::update(){
    try{synchronize_device();}catch(const std::exception& error){error_=error.what();stop();return;}
    if(!armed_)return;
    std::array<float,1024> buffer{};
    while(!recording_ || samples_<160000){
        const int available=SDL_GetAudioStreamAvailable(stream_.get());
        if(available<0){error_=SDL_GetError();stop();pcm_.clear();completed_.reset();return;}if(!available)break;
        const auto count=std::min({unsigned(available)/unsigned(sizeof(float)),unsigned(buffer.size()),recording_?160000-samples_:160000u});if(!count)break;
        const auto bytes=SDL_GetAudioStreamData(stream_.get(),buffer.data(),int(count*sizeof(float)));
        if(bytes<0){error_=SDL_GetError();stop();pcm_.clear();completed_.reset();return;}if(!bytes)break;
        const auto first=pcm_.size();
        for(int i=0;i<bytes/int(sizeof(float));++i){const auto sample=microphone_pcm(buffer[i],gain_);const double value=sample/32768.0;
            if(i==0)meter_sample_=sample;
            if(recording_){energy_+=value*value;pcm_.push_back(sample);++samples_;}else preroll_.push_back(sample);}
        if(recording_){
            const auto added=std::span<const std::int16_t>(pcm_).subspan(first);
            spectrum_.push(added);
            events_.push_back({MicrophoneUtteranceEvent::Type::audio,active_utterance_,{added.begin(),added.end()}});
        }
        // Retain only enough pre-button audio for event/device latency. The
        // former half second could include the preceding game dialogue
        // or microphone prompt in a push-to-talk command.
        else if(preroll_.size()>1280)preroll_.erase(preroll_.begin(),preroll_.end()-1280);
    }
    if(recording_ && samples_>=160000){
        const bool released=finishing_ || (!keyboard_ && !pad_);
        limited_=true;
        if(released)finish();else stop();
        return;
    }
    if(finishing_ && SDL_GetTicks()>=release_until_)finish();
}
void MicrophoneInput::cancel(){
    const auto utterance=active_utterance_;keyboard_=pad_=false;device_release_=false;bindings_.reset();stop();pcm_.clear();completed_.reset();limited_=false;active_utterance_=0;
    if(utterance)events_.push_back({MicrophoneUtteranceEvent::Type::cancel,utterance,{}});
}
std::optional<std::vector<std::int16_t>> MicrophoneInput::take_capture(){auto result=std::move(completed_);completed_.reset();return result;}
std::optional<MicrophoneUtteranceEvent> MicrophoneInput::take_event(){if(events_.empty())return {};auto result=std::move(events_.front());events_.pop_front();return result;}
std::string MicrophoneInput::status()const{
    if(!error_.empty())return "Microphone unavailable: "+error_;
    if(limited_)return "MIC 10-second limit reached | release "+action_hint(InputAction::speak)+" to recognize";
    if(finishing_)return "MIC finishing capture...";
    if(recording_)return "MIC recording | "+std::to_string(samples_*1000/16000)+" ms | level "+std::to_string(samples_?int(100*std::sqrt(energy_/samples_)):0)+"%";
    if(samples_)return "MIC captured "+std::to_string(samples_*1000/16000)+" ms | hold "+action_hint(InputAction::speak)+" to retry";
    return "Hold "+action_hint(InputAction::speak)+" to speak";
}
}
