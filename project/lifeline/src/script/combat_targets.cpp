#include "script/combat_targets.hpp"
#include "ui/combat_panel.hpp"
#include <bit>
#include <cmath>

namespace lifeline::script {
namespace {
unsigned pointer(float word){
    const float bytes=word*4;const double rounded=std::trunc(double(bytes+std::copysign(.5f,bytes)));
    if(!std::isfinite(rounded)||rounded<0||rounded>2147483647.||std::fmod(rounded,4))throw std::runtime_error("Invalid combat label pointer");return unsigned(rounded)/4;
}
unsigned read(const Program& p,const State& state,unsigned word){
    if(!state.data_contains(p,word))throw std::runtime_error("Combat label outside live script data");
    const auto i=state.data->find(word);return i==state.data->end()?p.word(word):std::bit_cast<unsigned>(i->second);
}
int integer(float value){
    if(!std::isfinite(value)||double(value)<-2147483648.||double(value)>2147483647.)throw std::runtime_error("Invalid combat HUD integer");
    return int(value);
}
}
Bindings combat_target_bindings(CombatTargets& targets,CombatPanel* panel){
    Bindings b;
    b.emplace(860,Binding{3,0,[&targets,panel](const Invocation& c){
        const auto message=integer(c.arguments[0]);
        if(panel)return Reply{false,float(panel->message(message,integer(c.arguments[1]),integer(c.arguments[2]),c.state && c.state->flags[400]))};
        if(message!=32771)throw std::runtime_error("Combat HUD integer message not implemented: "+std::to_string(message));
        targets.select(integer(c.arguments[1]),integer(c.arguments[2]));return Reply{false,0.f};
    }});
    for(unsigned opcode:{861u,862u})b.emplace(opcode,Binding{3,0,[&targets,opcode](const Invocation& c){
        const auto message=integer(c.arguments[0]);
        if(message!=32772)throw std::runtime_error("Combat HUD pointer message not implemented: "+std::to_string(message));
        const int enemy=integer(c.arguments[1]);if(enemy<0)return Reply{false,0.f};
        if(!c.state)throw std::runtime_error("Combat update requires script state");
        // 104168 truncates the word offset; 104250 uses the byte-address
        // resolver, which rounds half away from zero before checking alignment.
        const auto signed_word=opcode==861?integer(c.arguments[2]):0;
        if(opcode==861 && signed_word<0)throw std::runtime_error("Negative combat update pointer");
        const auto word=opcode==861?unsigned(signed_word):pointer(c.arguments[2]);
        if(!c.state->data_contains(c.program,word,14))throw std::runtime_error("Combat update outside live script data");
        std::array<float,14> values;
        for(unsigned i=0;i<14;++i)values[i]=std::bit_cast<float>(read(c.program,*c.state,word+i));
        targets.update(enemy,values);return Reply{false,0.f};
    }});
    b.emplace(863,Binding{3,0,[&targets](const Invocation& c){
        if(c.arguments[0]!=32774.f)throw std::runtime_error("Combat HUD message not implemented: "+std::to_string(c.arguments[0]));
        const float index=c.arguments[1];if(!std::isfinite(index)||index<0||index>=3)throw std::runtime_error("Combat label group outside 0..2");
        if(!c.state)throw std::runtime_error("Combat labels require script state");
        const auto table=pointer(c.arguments[2]);std::array<std::optional<unsigned>,9> words;
        // 104348 resolves the nine pointer values immediately. Text behind
        // those pointers remains live for subsequent selection messages.
        for(unsigned i=0;i<9;++i){const float value=std::bit_cast<float>(read(c.program,*c.state,table+i));if(value!=0)words[i]=pointer(value);}
        auto program=std::make_shared<Program>(c.program);State source;source.data=c.state->data;source.arrays=c.state->arrays;
        auto labels=[program,source=std::move(source),words]{
            CombatTargets::Labels out;
            for(unsigned i=0;i<9;++i)if(words[i]){
                std::string text;std::uint64_t byte=std::uint64_t(*words[i])*4;
                for(;;++byte){if(byte>2147483647u)throw std::runtime_error("Combat label string exceeds address space");const auto value=read(*program,source,unsigned(byte/4));const char ch=char((value>>(8*(byte%4)))&255);if(!ch)break;text+=ch;}
                out[i]=std::move(text);
            }
            return out;
        };
        labels(); // Validate before replacing the previous installed group.
        targets.install(unsigned(index),std::move(labels));return Reply{false,0.f};
    }});
    return b;
}
}
