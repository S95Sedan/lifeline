#include "speech/backend.hpp"
#include "speech/task.hpp"
#include "ui/combat_targets.hpp"
#include <algorithm>
#include <mutex>
#include <utility>

namespace lifeline {
bool SpeechTargetScope::allows(const std::string& phrase,const CommandGrammar* grammar)const{
    if(!enabled)return true;
    const auto normalized=normalize_spoken_word(phrase);
    if(normalized.empty())return true;
    bool target=std::binary_search(known_parts.begin(),known_parts.end(),normalized);
    if(!target&&grammar){
        const auto parsed=grammar->parse(phrase);
        // Audited across the installed battle VGP files: 60..92 are the
        // authored body-part phrase IDs. Actions, enemy-selection words and
        // commands remain available even if no label happens to display them.
        target=parsed.matched_words==1&&parsed.sequence[0]>=60&&parsed.sequence[0]<=92;
    }
    if(!target)return true;
    return std::binary_search(active_parts.begin(),active_parts.end(),normalized);
}
SpeechTargetScope battle_speech_target_scope(const CombatTargets& targets,bool panel_active){
    SpeechTargetScope scope;
    const int selected=targets.enemy();
    if(!panel_active||selected<0||selected>=int(targets.groups().size())||!targets.groups()[selected].source)return scope;
    scope.enabled=true;
    for(unsigned enemy=0;enemy<targets.groups().size();++enemy){
        const auto& group=targets.groups()[enemy];if(!group.source)continue;
        const auto labels=group.source();
        for(unsigned part=0;part<8;++part)if(labels[part]){
            const auto phrase=normalize_spoken_word(*labels[part]);if(phrase.empty())continue;
            scope.known_parts.push_back(phrase);
            if(enemy==unsigned(selected)&&group.states[part]==1)scope.active_parts.push_back(phrase);
        }
    }
    const auto order=[](auto& names){std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());};
    order(scope.known_parts);order(scope.active_parts);
    return scope;
}
namespace {
std::mutex configuration_mutex;
SpeechBackendConfiguration configured;
}

SpeechBackendConfiguration speech_backend_configuration(){
    std::scoped_lock lock(configuration_mutex);return configured;
}
void speech_backend_configuration(SpeechBackendConfiguration value){
    std::scoped_lock lock(configuration_mutex);configured=std::move(value);
}
const char* speech_backend_name(SpeechBackendKind){return "Original";}
}
