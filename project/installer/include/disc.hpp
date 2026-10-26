#pragma once
#include "common.hpp"
#include <fstream>

namespace lifeline::setup {
struct Extent {
    std::uint64_t offset = 0, size = 0;
};
class Disc {
    std::ifstream stream;
    std::map<std::string, Extent> root, index;
    Extent payload;
    Bytes elf;
    std::uint64_t total = 0;

  public:
    explicit Disc(const fs::path &iso);
    Bytes read(std::uint64_t offset, std::uint64_t size);
    Bytes file(const std::string &key);
    const Bytes &executable() const { return elf; }
    std::uint64_t size() const { return total; }
    const auto &files() const { return index; }
    Json identity() const;
};
std::map<std::string, Bytes> archive_members(View data);
} // namespace lifeline::setup
