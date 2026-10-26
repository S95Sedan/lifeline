#pragma once
#include "speech/word_search.hpp"
#include <limits>
#include <functional>
#include <optional>

namespace lifeline {
struct SpeechUtteranceWord {
    std::uint32_t context_id;
    std::int64_t cost;
    std::size_t begin,end;
};
struct SpeechUtteranceMatch {
    std::uint32_t context_id;
    std::int64_t total_cost,word_cost;
    std::size_t begin,end;
    std::vector<SpeechUtteranceWord> words;
};
// Native bounded command graph: authored word paths separated by optional .S.
// Keeps acoustic boundaries so surrounding silence cannot inflate word scores.
// Up to eight words retain separate boundaries for the authored semantic parser.
struct SpeechUtteranceLinks {
    std::vector<std::vector<unsigned>> predecessors;
    std::vector<bool> initial,final;
};
class SpeechUtteranceSearch {
    struct Token {
        std::int64_t cost=std::numeric_limits<std::int64_t>::max(),word_cost=0;
        std::size_t begin=0,end=0;
        int history=-1;
    };
    struct Path { SpeechWordPath definition; std::vector<Token> states; Token tail; };
    std::vector<Path> paths_;
    struct History {SpeechUtteranceWord word;int previous;};
    std::vector<History> history_;
    unsigned vocabulary_size_=0,max_words_=1;
    std::function<bool(const SpeechUtteranceWord&)> accept_word_;
    std::optional<SpeechUtteranceLinks> links_;
    std::size_t required_=0,frames_=0;
    std::uint16_t silence_;
    std::int64_t silence_cost_=0;
public:
    SpeechUtteranceSearch(std::vector<SpeechWordPath> words,std::uint16_t silence_state,unsigned max_words=1,
        std::function<bool(const SpeechUtteranceWord&)> accept_word={},std::optional<SpeechUtteranceLinks> links={});
    void push(std::span<const std::int16_t> costs);
    std::vector<SpeechUtteranceMatch> ranked(const std::function<bool(std::size_t,std::size_t)>& accepts={})const;
    std::int64_t silence_cost()const { return silence_cost_; }
};
}
