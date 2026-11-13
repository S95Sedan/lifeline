#include "speech/command_grammar.hpp"
#include "speech/task.hpp"
#include <algorithm>
#include <stdexcept>
#include <bit>

namespace lifeline {
namespace {
std::string folded(std::string value){for(auto& c:value)if(c>='A' && c<='Z')c=char(c-'A'+'a');return value;}
}
CommandGrammar::CommandGrammar(std::span<const std::uint8_t> bytes){
    const auto word=[&](std::size_t p){
        if(p>bytes.size() || bytes.size()-p<4)throw std::runtime_error("Truncated command grammar");
        return std::uint32_t(bytes[p]) | std::uint32_t(bytes[p+1])<<8 | std::uint32_t(bytes[p+2])<<16 | std::uint32_t(bytes[p+3])<<24;
    };
    if(word(0)!=0x20504756 || word(4)!=0x30302e32)throw std::runtime_error("Unsupported command grammar");
    const auto count=word(8),bid=word(12);
    if(!count || count>1024 || bid>bytes.size() || bid<16+std::size_t(count)*16 || word(bid)!=0x20444942 || word(bid+4)!=0x30302e31)throw std::runtime_error("Invalid command binding table");
    const auto entries=word(bid+8);if(entries>1024 || std::size_t(bid)+16+std::size_t(entries)*24>bytes.size())throw std::runtime_error("Truncated command bindings");
    const auto read_rules=[&](unsigned offset,std::vector<Rule>& destination){
        const auto size=word(offset+8);if(size>1024 || std::size_t(offset)+16+std::size_t(size)*24>bytes.size())throw std::runtime_error("Truncated semantic rule table");
        for(unsigned i=0;i<size;++i){Rule rule;const auto p=offset+16+i*24;for(unsigned field=0;field<8;++field)rule.fields[field]=std::bit_cast<std::int16_t>(std::uint16_t(word(p+(field/2)*4)>>(16*(field%2))));rule.command=std::bit_cast<std::int32_t>(word(p+16));destination.push_back(rule);}
    };
    read_rules(bid,rules_);
    const auto next=std::uint64_t(bid)+word(bid+12);
    if(next+16>bytes.size() || word(unsigned(next))!=0x5f444942 || word(unsigned(next)+4)!=0x204f5349)throw std::runtime_error("Invalid isolated command table");
    read_rules(unsigned(next),isolated_);
    for(unsigned i=0;i<count;++i){const auto p=16+i*16,flags=word(p),name=word(p+8);const auto category=std::int16_t(flags&65535);
        if(category<0 || category>=8 || name<16+count*16 || name>=bid)throw std::runtime_error("Invalid grammar phrase record");
        const auto extent=(word(p+4)&65535)*8u;
        // Room grammars retain zero-length reserved rows before the sorted
        // phrase records. Keep their slots so midpoint-search ordering stays
        // intact, but do not read the following phrase as their text/key.
        if(!extent){phrases_.push_back({"",category,std::bit_cast<std::int16_t>(std::uint16_t(flags>>16)),0});continue;}
        if(!extent || std::uint64_t(name)+extent>bid)throw std::runtime_error("Invalid grammar phrase extent");
        const auto end=std::find(bytes.begin()+name,bytes.begin()+name+extent,0);
        phrases_.push_back({folded(std::string(bytes.begin()+name,end)),category,std::bit_cast<std::int16_t>(std::uint16_t(flags>>16)),std::uint64_t(word(name))|(std::uint64_t(word(name+4))<<32)});
    }
    std::map<unsigned,std::uint16_t> semantics;
    for(unsigned i=0;i<entries;++i){const auto p=std::size_t(bid)+16+i*24;semantics[word(p)>>16]=std::uint16_t(word(p+16));}
    for(unsigned i=0;i<count;++i){const auto p=16+i*16,flags=word(p),name=word(p+8);
        if((flags&65535)!=1 || !(word(p+4)&65535))continue;
        if(name<16+count*16 || name>=bid)throw std::runtime_error("Command phrase outside string table");
        auto limit=bid;
        for(unsigned j=0;j<count;++j){const auto next=word(16+j*16+8);if(next>name && next<limit)limit=next;}
        const auto end=std::find(bytes.begin()+name,bytes.begin()+limit,0);
        const auto binding=semantics.find(flags>>16);if(binding==semantics.end())continue;
        const auto phrase=folded(std::string(bytes.begin()+name,end));
        if(phrase.empty())continue;
        const auto [at,inserted]=commands_.emplace(phrase,binding->second);
        if(!inserted && at->second!=binding->second){
            // Duplicate text may occupy distinct authored categories/rules.
            // Resolve it with the same ordered parser used for live input.
            const auto resolved=parse(phrase).command;
            if(resolved>=0&&resolved<=65535)at->second=std::uint16_t(resolved);
        }
        const auto normalized=normalize_spoken_word(phrase);
        if(!normalized.empty()){
            const auto [entry,added]=normalized_.emplace(normalized,binding->second);
            if(!added && entry->second!=binding->second)entry->second.reset();
        }
    }
}
std::optional<std::uint16_t> CommandGrammar::command(const std::string& phrase)const{
    const auto parsed=parse(phrase);if(parsed.command>=0 && parsed.command<=65535)return std::uint16_t(parsed.command);
    const auto exact=commands_.find(folded(phrase));if(exact!=commands_.end())return exact->second;
    const auto found=normalized_.find(normalize_spoken_word(phrase));
    return found==normalized_.end()?std::nullopt:found->second;
}
CommandGrammar::Parsed CommandGrammar::parse(const std::string& phrase)const{
    const std::array<std::string,1> words{phrase};return parse(words);
}
CommandGrammar::Parsed CommandGrammar::parse(std::span<const std::string> words)const{
    Parsed result;unsigned used=0;
    for(const auto& word:words){
        const auto key=folded(word);const Phrase* selected=nullptr;
        const Phrase* canonical=nullptr;
        for(const auto& entry:phrases_)if(entry.text==key){canonical=&entry;break;}
        if(!canonical){
            const auto normalized=normalize_spoken_word(word);
            for(const auto& entry:phrases_)if(!normalized.empty() && normalize_spoken_word(entry.text)==normalized){
                if(canonical && canonical->text!=entry.text){canonical=nullptr;break;}
                canonical=&entry;
            }
        }
        if(canonical){
            // Match the original midpoint search, backwards scan, then forward
            // scan amongst equal 64-bit prefixes. This matters when one phrase
            // can fill multiple grammar categories.
            int low=0,high=int(phrases_.size())-1,mid=-1;
            while(low<=high){const int at=(low+high)/2;if(phrases_[at].key<canonical->key)low=at+1;else if(phrases_[at].key>canonical->key)high=at-1;else{mid=at;break;}}
            const auto match=[&](int at){const auto& entry=phrases_[at];if(entry.text==canonical->text && result.categories[entry.category]==-1){selected=&entry;return true;}return false;};
            if(mid>=0){for(int at=mid;at>=0 && phrases_[at].key==canonical->key;--at)if(match(at))break;
                if(!selected)for(int at=mid;at<int(phrases_.size()) && phrases_[at].key==canonical->key;++at)if(match(at))break;}
        }
        if(!selected)continue;
        ++result.matched_words;
        result.categories[selected->category]=selected->id;
        if(used<result.sequence.size())result.sequence[used++]=selected->id;
    }
    if(result.categories[4]!=-1){
        for(const auto& rule:isolated_)if(rule.fields[4]==result.categories[4]){result.command=rule.command;break;}
        return result;
    }
    if(result.categories[0]==-1 && result.categories[1]==-1 && result.categories[2]==-1)return result;
    std::vector<const Rule*> candidates;
    for(const auto& rule:rules_){
        bool match=true;
        for(unsigned category:{2u,3u,1u,0u}){
            const auto input=result.categories[category],value=rule.fields[category];
            match&=input==-1?value<0:value==-2 || value==0 || value==input;
        }
        if(match)candidates.push_back(&rule);
    }
    if(candidates.empty())return result;
    const auto* chosen=candidates.front();
    // 1adc18 prefers a specified field over an unspecified (zero) field,
    // testing categories 3, 1, then 0. Otherwise keep the first binding.
    for(unsigned category:{3u,1u,0u}){
        bool zero=false;const Rule* positive=nullptr;
        for(const auto* rule:candidates){if(rule->fields[category]==0)zero=true;else if(rule->fields[category]>0)positive=rule;}
        if(positive)chosen=positive;
        if(zero && positive)break;
    }
    result.command=chosen->command;return result;
}
std::vector<std::string> CommandGrammar::phrases()const{
    std::vector<std::string> result;
    for(const auto& phrase:phrases_)if(!phrase.text.empty()&&std::find(result.begin(),result.end(),phrase.text)==result.end())result.push_back(phrase.text);
    return result;
}
}
