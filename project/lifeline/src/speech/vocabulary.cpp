#include "speech/vocabulary.hpp"
#include "speech/task.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace lifeline {
namespace {
bool printable_utf8(std::string_view text){
    for(std::size_t i=0;i<text.size();){
        const auto first=static_cast<unsigned char>(text[i++]);
        if(first<0x80){if(first<32||first==127)return false;continue;}
        unsigned count=0,code=0,minimum=0;
        if(first>=0xc2&&first<=0xdf){count=1;code=first&31;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){count=2;code=first&15;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){count=3;code=first&7;minimum=0x10000;}
        else return false;
        if(i+count>text.size())return false;
        while(count--){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}
        if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }
    return true;
}
}
SpeechVocabulary::SpeechVocabulary(std::span<const unsigned char> file){
    const auto invalid=[](){throw std::runtime_error("Invalid installed speech vocabulary; run the installer again");};
    if(file.empty() || file.size()>65536)invalid();
    std::istringstream input(std::string(reinterpret_cast<const char*>(file.data()),file.size()));
    const auto line=[&](){std::string text;std::getline(input,text);if(!text.empty() && text.back()=='\r')text.pop_back();return text;};
    if(line()!="LIFELINE_SPEECH_VOCABULARY\t1" || line()!="id\tkind\ttext")invalid();
    bool has_spoken=false;
    while(input.peek()!=std::char_traits<char>::eof()){
        const auto record=line();const auto first=record.find('\t'),second=first==std::string::npos?first:record.find('\t',first+1);
        if(first==std::string::npos || second==std::string::npos || words_.size()>=4096)invalid();
        unsigned id=0;const auto parsed=std::from_chars(record.data(),record.data()+first,id);
        if(parsed.ec!=std::errc{} || parsed.ptr!=record.data()+first || id!=words_.size())invalid();
        const auto kind=record.substr(first+1,second-first-1);auto text=record.substr(second+1);
        if((kind!="spoken" && kind!="control") || text.size()>768 || !printable_utf8(text))invalid();
        if(kind=="control"){if(!text.empty() && text!=".S" && text!="...")invalid();}
        else{if(text.empty() || text.front()=='.' || text.find_first_not_of(' ')==std::string::npos)invalid();has_spoken=true;}
        // The disc's English room vocabularies contain both Bathroom and
        // Washroom as separate acoustic labels for the same destination.
        // Their models are easily confused; expose the recognized alias as
        // the canonical game term so the grammar dispatches Bathroom.
        if(kind=="spoken"&&normalize_spoken_word(text)=="washroom")text="Bathroom";
        words_.push_back({id,kind=="control",text});
    }
    if(!has_spoken)invalid();
}
std::optional<unsigned> SpeechVocabulary::find(const std::string& spoken)const{
    const auto normalized=normalize_spoken_word(spoken);if(normalized.empty())return {};
    for(const auto& word:words_)if(!word.control && normalize_spoken_word(word.text)==normalized)return word.id;
    return {};
}
}
