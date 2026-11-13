#pragma once
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lifeline {
struct SpeechWord {
    unsigned id;
    bool control;
    std::string text;
};
// Installer-converted UTF-8 table. IDs are WRD indices, not GSAPI function IDs.
class SpeechVocabulary {
    std::vector<SpeechWord> words_;
public:
    explicit SpeechVocabulary(std::span<const unsigned char> file);
    const std::vector<SpeechWord>& words()const{return words_;}
    std::optional<unsigned> find(const std::string& spoken)const;
};
}
