#include "game/objects.hpp"
#include "script/sound.hpp"
#include <cmath>
#include <sstream>
namespace lifeline::script {
void Objects::clear_motion_sound(unsigned slot){
    auto& table=motion_sounds_.at(slot);
    if(table.tracked){if(!sound_)throw std::runtime_error("Object sound service is not connected");sound_->stop_channel(table.channel,true);}
    table={};
}
void Objects::play_motion_sound(unsigned slot,unsigned animation){
    auto& table=motion_sounds_.at(slot);
    for(const auto& cue:table.cues)if(cue.animation==animation){
        if(!sound_)throw std::runtime_error("Object sound service is not connected");
        if(table.tracked)sound_->stop_channel(table.channel,true);
        sound_->play(table.bank,cue.group,cue.sequence,cue.tracked?std::optional<unsigned>(table.channel):std::nullopt);
        table.tracked=cue.tracked;return;
    }
}
Bindings Objects::motion_sound_bindings(){
    Bindings bindings;
    bindings.emplace(347,Binding{4,0,[this](const Invocation& c){
        const auto object=slot(std::nearbyint(c.arguments[0]));
        const auto name=std::nearbyint(c.arguments[1]),channel=std::nearbyint(c.arguments[2]),bank=std::nearbyint(c.arguments[3]);
        if(!std::isfinite(name) || name<0 || double(name)>=c.program.size() || !std::isfinite(channel) || channel<0 || channel>=8 || !std::isfinite(bank) || bank<0 || bank>=6)throw std::runtime_error("Invalid object motion sound parameters");
        // Original 347 uses the same first-loaded archive lookup as its bank.
        const auto* files=resources_.first_loaded_files(c.program.string(unsigned(name)));const ResourceFile* selected=nullptr;
        if(!files)throw std::runtime_error("Motion sound table is not loaded");
        for(const auto& file:*files)if(file.conversion=="motion-sounds-v1"){if(selected)throw std::runtime_error("Ambiguous motion sound table");selected=&file;}
        if(!selected || !selected->bytes)throw std::runtime_error("Motion sound table is not converted");
        std::istringstream input(std::string(selected->bytes->begin(),selected->bytes->end()));std::string line;
        if(!std::getline(input,line) || line!="LIFELINE_MOTION_SOUNDS\t1")throw std::runtime_error("Unsupported motion sound table");
        MotionSound replacement;replacement.channel=unsigned(channel);replacement.bank=unsigned(bank);
        while(std::getline(input,line)){
            std::istringstream row(line);std::string kind,extra;unsigned animation,group,sequence,tracked;
            if(!(row>>kind>>animation>>group>>sequence>>tracked) || kind!="cue" || animation>65535 || group>65535 || sequence>65535 || tracked>1 || row>>extra)throw std::runtime_error("Invalid motion sound record");
            replacement.cues.push_back({animation,group,sequence,tracked!=0});
        }
        clear_motion_sound(object);motion_sounds_[object]=std::move(replacement);return Reply{false,float(object)};
    }});
    bindings.emplace(348,Binding{1,0,[this](const Invocation& c){const auto object=slot(std::nearbyint(c.arguments[0]));clear_motion_sound(object);return Reply{false,float(object)};}});
    return bindings;
}
}
