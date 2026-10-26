#pragma once
#include "common.hpp"
namespace lifeline::setup {
struct PackageFile {
    std::string name, sha256;
    std::uint64_t size = 0;
    View compressed;
};
class Package {
    Bytes storage;
    std::vector<PackageFile> entries;

  public:
    explicit Package(Bytes bytes);
    Package(const Package &) = delete;
    Package &operator=(const Package &) = delete;
    Package(Package &&) = default;
    static Package embedded();
    Bytes get(const std::string &name) const;
    const auto &files() const { return entries; }
};
Bytes make_package(const std::map<std::string, Bytes> &files);
} // namespace lifeline::setup
