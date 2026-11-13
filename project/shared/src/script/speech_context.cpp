#include "script/speech_context.hpp"
#include <bit>
#include "speech/acoustic_model.hpp"

namespace lifeline::script {
void SpeechContext::language(const std::string& member){
    const auto bytes=resources_.find_raw(member).bytes;
    if(model_){OriginalSpeechData validate(*bytes,*model_);}
    else {
        if(bytes->size()<0xe0||(*bytes)[0]!=3||(*bytes)[1]!=16||(*bytes)[2]||(*bytes)[3])throw std::runtime_error("Invalid acoustic language resource");
        const auto size=std::uint32_t((*bytes)[4])|std::uint32_t((*bytes)[5])<<8|std::uint32_t((*bytes)[6])<<16|std::uint32_t((*bytes)[7])<<24;
        if(size<0xe0||size>bytes->size())throw std::runtime_error("Invalid acoustic language size");
    }
    acoustics_=bytes;++revision_;
    if(language_reset_)language_reset_();
}
void SpeechContext::select(const std::string& context,const std::string& words){
    const auto model=resources_.find_raw(context).bytes;
    const auto& files=resources_.find_files(words);
    if(files.size()!=1 || files[0].conversion!="speech-vocabulary-v1" || !files[0].bytes)throw std::runtime_error("Speech context needs installed vocabulary");
    SpeechVocabulary validate(*files[0].bytes);
    if(acoustics_){OriginalSpeechData validate_model(*acoustics_,*model);}
    model_=model;vocabulary_=files[0].bytes;name_=context;++revision_;
}
void SpeechContext::grammar(const std::string& member){grammar_.emplace(*resources_.find_raw(member).bytes);++revision_;}
Bindings SpeechContext::bindings(){
    Bindings result;
    result.emplace(595,Binding{0,1,[this](const Invocation& c){
        const auto name=c.program.string(c.operands[0]);
        // 142e88..142ea0 logs a missing resource and advances without replacing
        // the current recognizer; it does not clear the retained language.
        if(resources_.first_loaded_files(name))language(name);return Reply{};
    }});
    // 1447d8 -> recognizer-thread message 15: destroy/recreate the engine
    // with its retained model. The scene consumes revision changes by cancelling
    // the old capture/task and constructing a fresh native recognizer.
    result.emplace(627,Binding{0,0,[this](const Invocation&){++revision_;return Reply{false,0.f};}});
    result.emplace(596,Binding{0,2,[this](const Invocation& c){select(c.program.string(c.operands[0]),c.program.string(c.operands[1]));return Reply{};}});
    result.emplace(606,Binding{0,1,[this](const Invocation& c){grammar(c.program.string(c.operands[0]));return Reply{};}});
    result.emplace(609,Binding{0,4,[this](const Invocation& c){
        // 1440d0 -> 1ae018 copies four inline integer words to parser +24.
        // The shipped 1ad8b0/1adc18 parser uses fixed category priorities.
        for(unsigned i=0;i<4;++i)grammar_preferences_[i]=std::bit_cast<std::int32_t>(c.operands[i]);
        return Reply{};
    }});
    for(unsigned opcode=601;opcode<=604;++opcode)result.emplace(opcode,Binding{0,1,[this,opcode](const Invocation& c){
        const auto value=std::bit_cast<std::int32_t>(c.operands[0]);
        if(parameters_[opcode-601]!=value){parameters_[opcode-601]=value;++revision_;}
        return Reply{};
    }});
    return result;
}
}
