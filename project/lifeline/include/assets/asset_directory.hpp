#pragma once
#include "core/bytes.hpp"
#include <filesystem>
#include <string_view>

namespace lifeline {
// Individual external files. Manifest provenance is separate from runtime data;
// artists and developers can replace files without rebuilding the executable.
class AssetDirectory {
    std::filesystem::path root_;
public:
    explicit AssetDirectory(const std::filesystem::path&);
    std::filesystem::path resolve(const std::string& logical_path) const;
    Bytes read(const std::string& logical_path) const;
    std::size_t verify_readable() const;
};
}
