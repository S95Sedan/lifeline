#pragma once
#include "core/bytes.hpp"
#include <bit>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace lifeline {
class StateWriter {
    Bytes data_;
public:
    void word(std::uint32_t value){for(unsigned i=0;i<4;++i)data_.push_back(std::uint8_t(value>>(8*i)));}
    void bytes(std::span<const std::uint8_t> bytes){data_.insert(data_.end(),bytes.begin(),bytes.end());}
    void string(const std::string& value){word(std::uint32_t(value.size()));bytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
    Bytes take(){return std::move(data_);}
};
class StateReader {
    std::span<const std::uint8_t> data_;
public:
    explicit StateReader(std::span<const std::uint8_t> data):data_(data){}
    std::span<const std::uint8_t> bytes(std::size_t size){if(size>data_.size())throw std::runtime_error("Truncated native game context");const auto result=data_.first(size);data_=data_.subspan(size);return result;}
    std::uint32_t word(){const auto value=bytes(4);return value[0]|std::uint32_t(value[1])<<8|std::uint32_t(value[2])<<16|std::uint32_t(value[3])<<24;}
    std::string string(){const auto value=bytes(word());return {value.begin(),value.end()};}
    bool empty()const{return data_.empty();}
};
}
