#include "speech/context_adapter.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace lifeline {
namespace {
std::vector<std::string> words(const std::string& value){
    std::istringstream input(value);std::vector<std::string> result;std::string word;
    while(input>>word)result.push_back(std::move(word));return result;
}
std::string join(std::span<const std::string> input,std::size_t first,std::size_t count){
    std::string result;for(std::size_t i=0;i<count;++i){if(i)result+=' ';result+=input[first+i];}return result;
}
}

std::string normalize_transcript(std::string_view input){
    std::string result;bool space=true;
    for(unsigned char c:input){
        if(c>='A'&&c<='Z')c=unsigned char(c-'A'+'a');
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='\''){result+=char(c);space=false;}
        else if(std::isspace(c)||c=='.'||c==','||c=='!'||c=='?'){if(!space&&!result.empty()){result+=' ';space=true;}}
        else return {};
    }
    if(!result.empty()&&result.back()==' ')result.pop_back();return result;
}

std::vector<std::string> speech_context_phrases(const SpeechContextSnapshot& context){
    std::vector<std::string> result;
    if(context.grammar)result=context.grammar->phrases();
    for(const auto& word:context.vocabulary.words())if(!word.control&&!word.text.empty()){
        const auto phrase=normalize_transcript(word.text);
        if(!phrase.empty()&&std::find(result.begin(),result.end(),phrase)==result.end())result.push_back(phrase);
    }
    return result;
}

AdaptedSpeech adapt_transcript(std::string_view transcript,const SpeechContextSnapshot& context){
    AdaptedSpeech result;result.normalized=normalize_transcript(transcript);
    if(result.normalized.empty()){
        const bool has_non_space=std::any_of(transcript.begin(),transcript.end(),[](unsigned char c){return !std::isspace(c);});
        result.diagnostic=has_non_space?"Transcript contains unsupported characters":"Improved recognizer returned no final transcript";return result;
    }
    const auto tokens=words(result.normalized),phrases=speech_context_phrases(context);
    struct Path {unsigned state=0;std::vector<std::string> components;}; // 0 absent, 1 unique, 2 ambiguous
    std::vector<Path> paths(tokens.size()+1);paths[0].state=1;
    for(std::size_t at=0;at<tokens.size();++at)if(paths[at].state==1){
        for(const auto& phrase:phrases){const auto phrase_words=words(phrase);if(phrase_words.empty()||at+phrase_words.size()>tokens.size())continue;
            bool same=true;for(std::size_t i=0;i<phrase_words.size();++i)same&=tokens[at+i]==phrase_words[i];if(!same)continue;
            auto candidate=paths[at].components;candidate.push_back(phrase);
            auto& destination=paths[at+phrase_words.size()];
            if(!destination.state||candidate.size()<destination.components.size()){destination.state=1;destination.components=std::move(candidate);}
            else if(destination.state==1&&candidate.size()==destination.components.size()&&candidate!=destination.components){destination.state=2;destination.components.clear();}
        }
    }
    if(paths.back().state!=1){result.diagnostic=paths.back().state==2?"Transcript has multiple active-phrase segmentations":"Transcript is not an unambiguous sequence of active phrases";return result;}
    result.components=std::move(paths.back().components);result.complete=!result.components.empty();
    if(result.components.size()>8){result.components.clear();result.complete=false;result.diagnostic="Transcript exceeds the original eight-component interface";return result;}
    if(!result.complete)result.diagnostic="Transcript contains no active phrase";
    return result;
}
}
