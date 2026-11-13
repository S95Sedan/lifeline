#include "speech/utterance_search.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace lifeline {
SpeechUtteranceSearch::SpeechUtteranceSearch(std::vector<SpeechWordPath> words,std::uint16_t silence,unsigned max_words,
    std::function<bool(const SpeechUtteranceWord&)> accept_word,std::optional<SpeechUtteranceLinks> links)
    :vocabulary_size_(unsigned(words.size())),max_words_(max_words),accept_word_(std::move(accept_word)),links_(std::move(links)),required_(std::size_t(silence)+1),silence_(silence) {
    if(!max_words||max_words>8)throw std::invalid_argument("Invalid speech sequence capacity");
    if(words.empty() || words.size()>65536) throw std::invalid_argument("Invalid utterance word count");
    if(links_){
        if(links_->predecessors.size()!=words.size()||links_->initial.size()!=words.size()||links_->final.size()!=words.size())throw std::invalid_argument("Invalid utterance graph shape");
        for(const auto& list:links_->predecessors)for(auto index:list)if(index>=words.size())throw std::invalid_argument("Invalid utterance graph edge");
    }
    std::size_t total=0; std::unordered_set<std::uint32_t> ids;
    for(auto& word:words) {
        if(word.states.empty() || word.states.size()>65536 || !ids.insert(word.context_id).second)
            throw std::invalid_argument("Invalid utterance word path");
        total+=word.states.size();
        if(total>1000000) throw std::invalid_argument("Utterance word graph too large");
        for(auto state:word.states) required_=std::max(required_,std::size_t(state)+1);
        auto tokens=std::vector<Token>(word.states.size());
        paths_.push_back({std::move(word),std::move(tokens),{}});
    }
    paths_.reserve(std::size_t(vocabulary_size_)*max_words_);
    for(unsigned level=1;level<max_words_;++level)for(unsigned i=0;i<vocabulary_size_;++i){
        const auto definition=paths_[i].definition;
        paths_.push_back({definition,std::vector<Token>(definition.states.size()),{}});
    }
}
void SpeechUtteranceSearch::push(std::span<const std::int16_t> costs) {
    constexpr auto unavailable=std::numeric_limits<std::int64_t>::max();
    if(costs.size()<required_ || costs[silence_]==0x7fff) throw std::invalid_argument("Missing utterance costs");
    if(frames_>=1000000) throw std::length_error("Speech utterance too long");
    for(const auto& path:paths_) for(auto state:path.definition.states)
        if(costs[state]==0x7fff) throw std::invalid_argument("Uncomputed utterance cost");
    // Freeze completed prefixes before this frame. A new word must consume
    // new audio, including when repeating the same command without a pause.
    std::vector<Token> prefixes(paths_.size());
    for(unsigned i=0;i<vocabulary_size_;++i)if(!links_||links_->initial[i])prefixes[i].cost=silence_cost_;
    std::vector<int> saved(paths_.size(),-1);
    for(unsigned level=1;level<max_words_;++level){
        for(unsigned destination=0;destination<vocabulary_size_;++destination){
            unsigned best=unsigned(paths_.size());
            const auto consider=[&](unsigned i){const auto index=(level-1)*vocabulary_size_+i;const auto& p=paths_[index];if(p.tail.cost!=unavailable&&(best==paths_.size()||p.tail.cost<paths_[best].tail.cost))best=index;};
            if(links_){for(auto i:links_->predecessors[destination])consider(i);}else for(unsigned i=0;i<vocabulary_size_;++i)consider(i);
            if(best!=paths_.size()){
                const auto& p=paths_[best];auto& prefix=prefixes[level*vocabulary_size_+destination];prefix.cost=p.tail.cost;
                if(saved[best]<0){saved[best]=int(history_.size());history_.push_back({{p.definition.context_id,p.tail.word_cost,p.tail.begin,p.tail.end},p.tail.history});}
                prefix.history=saved[best];
            }
        }
    }
    for(unsigned path_index=0;path_index<paths_.size();++path_index) {
        auto& path=paths_[path_index];const auto& prefix=prefixes[path_index];
        if(path.tail.cost!=unavailable) path.tail.cost+=costs[silence_];
        for(std::size_t pos=path.states.size();pos>0;) {
            const auto i=--pos;
            Token predecessor=path.states[i];
            if(i && path.states[i-1].cost<predecessor.cost) predecessor=path.states[i-1];
            else if(!i && prefix.cost<predecessor.cost) predecessor={prefix.cost,0,frames_,frames_,prefix.history};
            if(predecessor.cost!=unavailable) {
                const auto cost=costs[path.definition.states[i]];
                predecessor.cost+=cost; predecessor.word_cost+=cost; predecessor.end=frames_+1;
            }
            path.states[i]=predecessor;
        }
        const auto& end=path.states.back();
        if(end.cost<path.tail.cost&&(!accept_word_||accept_word_({path.definition.context_id,end.word_cost,end.begin,end.end})))path.tail=end;
    }
    silence_cost_+=costs[silence_]; ++frames_;
}
std::vector<SpeechUtteranceMatch> SpeechUtteranceSearch::ranked(const std::function<bool(std::size_t,std::size_t)>& accepts)const {
    std::vector<SpeechUtteranceMatch> result;
    for(unsigned i=0;i<paths_.size();++i){const auto& path=paths_[i];if((!links_||links_->final[i%vocabulary_size_])&&path.tail.cost!=std::numeric_limits<std::int64_t>::max()){
        std::vector<SpeechUtteranceWord> words{{path.definition.context_id,path.tail.word_cost,path.tail.begin,path.tail.end}};
        for(int previous=path.tail.history;previous>=0;previous=history_[previous].previous)words.push_back(history_[previous].word);
        std::reverse(words.begin(),words.end());const auto begin=words.front().begin;
        if(accepts&&!accepts(begin,path.tail.end))continue;
        std::int64_t word_cost=0;for(const auto& word:words)word_cost+=word.cost;
        result.push_back({path.definition.context_id,path.tail.cost,word_cost,begin,path.tail.end,std::move(words)});
    }}
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.total_cost<b.total_cost;});
    return result;
}
}
