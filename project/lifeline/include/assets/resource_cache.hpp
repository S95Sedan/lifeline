#pragma once
#include "assets/asset_directory.hpp"
#include <memory>

namespace lifeline {
// Immutable file payloads only. Logical archive membership and byte handles
// belong to Resources, independently of this bounded read cache.
class ResourceCache {
    struct Impl;
    std::shared_ptr<Impl> impl_;
public:
    explicit ResourceCache(const AssetDirectory&,std::size_t limit=64*1024*1024);
    std::shared_ptr<const Bytes> read(const std::string& path);
};
}
