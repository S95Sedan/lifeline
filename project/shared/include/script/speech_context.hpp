#pragma once
#include "script/runtime.hpp"
#include "assets/resources.hpp"
#include "speech/command_grammar.hpp"
#include "speech/vocabulary.hpp"
#include <algorithm>

namespace lifeline::script {
class SpeechContext {
    Resources& resources_;
    std::shared_ptr<const Bytes> model_,vocabulary_,acoustics_;
    std::optional<CommandGrammar> grammar_;
    std::string name_;
    unsigned revision_=0;
    std::function<void()> language_reset_;
    std::array<std::int32_t,4> parameters_{};
    std::array<std::int32_t,4> grammar_preferences_{2,3,1,0};
public:
    explicit SpeechContext(Resources& resources):resources_(resources){}
    void select(const std::string& context,const std::string& words);
    void language(const std::string& member);
    void on_language_reset(std::function<void()> callback){language_reset_=std::move(callback);}
    const Bytes* acoustics()const{return acoustics_.get();}
    void grammar(const std::string& member);
    Bindings bindings();
    unsigned revision()const{return revision_;}
    bool ready()const{return model_&&vocabulary_&&grammar_.has_value();}
    const std::string& name()const{return name_;}
    const Bytes& model()const{return *model_;}
    SpeechVocabulary vocabulary()const{return SpeechVocabulary(*vocabulary_);}
    const CommandGrammar& grammar()const{return grammar_.value();}
    const auto& parameters()const{return parameters_;}
    int acceptance_threshold()const{return parameters_[3]>0?std::clamp(parameters_[3],0,10000):4000;}
};
}
