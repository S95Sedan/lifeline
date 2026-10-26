#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <optional>
#include <map>

namespace lifeline::script {
// Game script addresses are word offsets, not PS2 addresses.
class Program {
    std::string name_;
    std::vector<std::uint32_t> words_;
    std::map<std::uint32_t,std::pair<unsigned,unsigned>> battle_helpers_;
public:
    Program(std::string name,std::span<const std::uint8_t> bytes);
    const std::string& name() const {return name_;}
    std::uint32_t word(std::uint32_t address) const;
    std::string string(std::uint32_t address) const;
    std::size_t size() const {return words_.size();}
    const auto& battle_helpers()const{return battle_helpers_;}
};
class EventTable {
    std::vector<std::uint32_t> entries_;
    std::uint32_t leading_entry_=0;
public:
    EventTable(const Program&,std::span<const std::uint8_t> bytes);
    std::optional<std::uint32_t> entry(unsigned event) const;
    // Loader 13c374..13c3b4 starts slot zero here, then advances the table pointer.
    std::uint32_t startup() const {return leading_entry_;}
    std::size_t size() const {return entries_.size();}
};
}
