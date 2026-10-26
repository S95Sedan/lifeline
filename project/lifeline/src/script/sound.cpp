#include "script/sound.hpp"
#include "script/save_menu.hpp"
#include <algorithm>
#include <cmath>

namespace lifeline::script {
void Sound::pause(bool paused){
    for(auto& bank:banks_)if(bank)bank->pause(paused);
    for(auto& channel:channels_)if(channel&&channel->bank)channel->bank->pause(paused);
    for(auto& stream:streams_)if(stream.audio)stream.audio->pause(paused||stream.paused);
    for(auto& stream:fading_streams_)if(stream.audio)stream.audio->pause(paused||stream.paused);
    for(auto& stream:direct_streams_)if(stream.audio)stream.audio->pause(paused||stream.paused);
}
namespace {
unsigned integer(float value,unsigned limit){if(!std::isfinite(value) || value<0 || value>=limit || value!=std::floor(value))throw std::runtime_error("Invalid sound parameter");return unsigned(value);}
}
void Sound::select(unsigned bank,const std::string& name){
    if(bank>=banks_.size())throw std::runtime_error("Invalid sound bank slot");
    // 698 -> 1f23b8 uses 1c6e50 for bank members (1f24e0/1f2504),
    // selecting the first loaded archive slot, not requiring a unique name.
    const auto* files=resources_.first_loaded_files(name+".hd");
    if(!files)throw std::runtime_error("Sound bank is not loaded: "+name);
    std::string descriptor;
    for(const auto& file:*files)if(std::filesystem::path(file.path).filename()=="bank.tsv"){
        if(file.conversion!="sound-bank-pcm-v1" || !file.bytes || !descriptor.empty())throw std::runtime_error("Invalid installed sound bank");
        descriptor=file.path;
    }
    if(descriptor.empty())throw std::runtime_error("Sound bank has no installed sequence table: "+name);
    auto replacement=std::make_shared<SoundPlayback>(assets_,descriptor,silent_);
    replacement->reverb_preset(reverb_preset_);
    replacement->reverb_depth(reverb_depth_);
    for(auto& channel:channels_)if(channel && channel->bank==banks_[bank])channel.reset();
    banks_[bank]=std::move(replacement);
    bank_names_[bank]=name;
}
unsigned Sound::play(unsigned bank,unsigned group,unsigned sequence,std::optional<unsigned> channel,bool track_load){
    if(track_load)load_cue_.reset();
    auto& sound=banks_.at(bank);if(!sound)throw std::runtime_error("Sound bank is not selected");
    if(channel){auto& current=channels_.at(*channel);if(current){current->bank->stop(current->cue,false);current.reset();}}
    // Original scripts may request deliberately sparse sequence-table slots.
    // ROOM_G2A group 1 is 0xffff in the disc header, but the Gustav-locker
    // callback still requests (1,2). The console service treats this as a
    // silent play request; it must not terminate the authored callback.
    if(!sound->contains(group,sequence))return next_handle_++;
    std::uint64_t cue=0;
    try{cue=sound->play(group,sequence);}
    catch(const std::runtime_error& error){throw std::runtime_error(std::string(error.what())+" (bank "+std::to_string(bank)+" "+bank_names_[bank]+", group "+std::to_string(group)+", sequence "+std::to_string(sequence)+")");}
    if(channel)channels_[*channel]=Instance{sound,cue};
    if(track_load)load_cue_=Instance{sound,cue};
    return next_handle_++;
}
void Sound::unload(unsigned bank){
    auto& selected=banks_.at(bank);if(!selected)return;
    for(auto& channel:channels_)if(channel && channel->bank==selected)channel.reset();
    selected.reset();bank_names_[bank].clear();
}
void Sound::stop_channel(unsigned id,bool release){auto& channel=channels_.at(id);if(channel){channel->bank->stop(channel->cue,release);channel.reset();}}
void Sound::stop_all(bool release){
    // 1f27a8 stops every sequence, including those without script channel
    // handles. Streamed music/voices use a separate original service.
    for(auto& bank:banks_)if(bank)bank->stop_all(release);
    for(auto& channel:channels_)channel.reset();
}
bool Sound::effects_active()const{
    return std::any_of(banks_.begin(),banks_.end(),[](const auto& bank){return bank && bank->active();});
}
bool Sound::effect_active(unsigned bank,unsigned group,unsigned program,unsigned key)const{
    return banks_.at(bank)&&banks_[bank]->active(group,program,key);
}
Bindings Sound::bindings(){
    Bindings result=stream_bindings();
    result.emplace(load_cue_play_opcode,Binding{3,0,[this](const Invocation& c){
        if(!has_native_save_menu(c.program.name()))throw std::runtime_error("Load cue outside storage menu");
        return Reply{false,float(play(integer(c.arguments[0],6),integer(c.arguments[1],65536),integer(c.arguments[2],65536),{},true))};
    }});
    result.emplace(load_cue_wait_opcode,Binding{0,0,[this](const Invocation& c){
        if(!has_native_save_menu(c.program.name()))throw std::runtime_error("Load cue wait outside storage menu");
        if(load_cue_&&load_cue_->bank->cue_pending(load_cue_->cue))return Reply{true,{}};
        load_cue_.reset();return Reply{};
    }});
    result.emplace(747,Binding{0,1,[this](const Invocation& c){
        // 138de0 -> 1efaa0 selects the effect mode on both audio cores.
        if(c.operands[0]<0||c.operands[0]>9)throw std::runtime_error("Invalid room reverb preset");
        reverb_preset_=unsigned(c.operands[0]);
        for(auto& bank:banks_)if(bank)bank->reverb_preset(reverb_preset_);
        return Reply{};
    }});
    result.emplace(748,Binding{0,1,[this](const Invocation& c){
        // 138e38 -> 1efad8: integer scale to the signed 15-bit wet volume.
        if(c.operands[0]<0||c.operands[0]>127)throw std::runtime_error("Invalid room reverb depth");
        reverb_depth_=float(c.operands[0]*32767/127)/32768.f;
        for(auto& bank:banks_)if(bank)bank->reverb_depth(reverb_depth_);
        return Reply{};
    }});
    for(unsigned opcode:{699u,700u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& c){
        const float value=c.arguments[0];if(!std::isfinite(value))throw std::runtime_error("Invalid sound bank release index");
        const float first=std::nearbyint(value);
        // Both original handlers ignore indices outside the six script slots.
        // 700 releases the suffix beginning at the requested slot.
        if(first>=0 && first<6)for(unsigned bank=unsigned(first);bank<(opcode==699?unsigned(first)+1:6);++bank)unload(bank);
        return Reply{false,0.f};
    }});
    result.emplace(698,Binding{2,0,[this](const Invocation& c){const auto bank=integer(c.arguments[0],6),name=integer(c.arguments[1],unsigned(c.program.size()));select(bank,c.program.string(name));return Reply{false,float(bank)};}});
    result.emplace(702,Binding{3,0,[this](const Invocation& c){return Reply{false,float(play(integer(c.arguments[0],6),integer(c.arguments[1],65536),integer(c.arguments[2],65536)))};}});
    result.emplace(703,Binding{4,0,[this](const Invocation& c){return Reply{false,float(play(integer(c.arguments[1],6),integer(c.arguments[2],65536),integer(c.arguments[3],65536),integer(c.arguments[0],8)))};}});
    result.emplace(704,Binding{2,0,[this](const Invocation& c){stop_channel(integer(c.arguments[0],8),integer(c.arguments[1],2)!=0);return Reply{false,0.f};
    }});
    result.emplace(705,Binding{1,0,[this](const Invocation& c){
        const float value=c.arguments[0];
        if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)throw std::runtime_error("Invalid sound stop mode");
        // Zero additionally zeros each voice's volume before key-off;
        // any nonzero converted integer leaves its release envelope audible.
        stop_all(std::int32_t(value)!=0);return Reply{false,0.f};
    }});
    return result;
}
void Sound::advance(){
    // Direct CAppStream requests have no script music-slot owner. Installed
    // PCM is ready on the first update, then reports the original playing (3)
    // and teardown (4/5/6) application states before releasing its slot.
    for(auto& stream:direct_streams_){
        if(stream.state>=4){++stream.state;if(stream.state==5)stream.driver=-1;if(stream.state>6)stream.audio.reset();continue;}
        if(stream.state==1){stream.state=3;continue;}
        stream.audio->advance();if(!stream.audio->active())stream.state=4;
    }
    std::erase_if(direct_streams_,[](const auto& s){return !s.audio;});
    for(auto& bank:banks_)if(bank)bank->advance();
    for(auto& stream:streams_)if(stream.audio)stream.audio->advance();
    for(auto& stream:fading_streams_)stream.audio->advance();
    std::erase_if(fading_streams_,[](const auto& stream){return !stream.audio->active();});
}
}
