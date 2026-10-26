#pragma once
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace lifeline::setup {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
using Json = nlohmann::ordered_json;
inline void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);
std::string path_text(const fs::path &path);
Bytes read_file(const fs::path &path);
void write_file(const fs::path &path, View bytes);
void write_text(const fs::path &path, std::string_view text);
std::string sha256(View bytes);
std::string file_sha256(const fs::path &path);
Json read_json(const fs::path &path);
View slice(View data, std::size_t offset, std::size_t length);
std::uint32_t u32(View data, std::size_t offset);
std::uint16_t u16(View data, std::size_t offset);
float f32(View data, std::size_t offset);
std::string fixed_string(View data, std::size_t offset, std::size_t maximum);
void put16(Bytes &data, std::uint16_t value);
void put32(Bytes &data, std::uint32_t value);
void put64(Bytes &data, std::uint64_t value);
void append(Bytes &destination, View source);
Bytes bytes(std::string_view text);
bool reparse(const fs::path &path);
fs::path validate_relative_path(std::string_view relative);
fs::path safe_path(const fs::path &root, std::string_view relative);
void check_tree(const fs::path &root);
fs::path executable_path();
fs::path default_destination();
fs::path find_iso();
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Installation cancelled. Your existing installation is preserved.") {}
};
struct Report {
    std::function<void(const std::wstring &, int)> status;
    std::atomic_bool *cancel = nullptr;
    void checkpoint() const {
        if (cancel && cancel->load())
            throw Cancelled();
    }
    void update(const std::wstring &text, int progress) const {
        checkpoint();
        if (status)
            status(text, progress);
    }
};
} // namespace lifeline::setup
