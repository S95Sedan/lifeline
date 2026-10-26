#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <array>
#include <vector>

namespace lifeline {
// VGP phrase IDs resolve to authored semantic IDs, never inferred voice cues.
class CommandGrammar {
    struct Phrase {std::string text;std::int16_t category,id;std::uint64_t key;};
    struct Rule {std::array<std::int16_t,8> fields;std::int32_t command;};
    std::vector<Phrase> phrases_;
    std::vector<Rule> rules_,isolated_;
    std::map<std::string,std::uint16_t> commands_;
    std::map<std::string,std::optional<std::uint16_t>> normalized_;
public:
    struct Parsed {
        std::int32_t command=-1;
        unsigned matched_words=0;
        std::array<std::int16_t,8> categories{-1,-1,-1,-1,-1,-1,-1,-1};
        std::array<std::int16_t,8> sequence{-1,-1,-1,-1,-1,-1,-1,-1};
    };
    explicit CommandGrammar(std::span<const std::uint8_t>);
    std::optional<std::uint16_t> command(const std::string&) const;
    Parsed parse(std::span<const std::string> words)const;
    Parsed parse(const std::string& phrase)const;
    std::vector<std::string> phrases()const;
    const auto& commands()const{return commands_;}
};
}
