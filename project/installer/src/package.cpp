#include "package.hpp"
#include <compressapi.h>
namespace lifeline::setup {
namespace {
std::uint64_t u64(View data, std::size_t p) { return u32(data, p) | (std::uint64_t(u32(data, p + 4)) << 32); }
struct Compressor {
    COMPRESSOR_HANDLE value = nullptr;
    Compressor() {
        require(CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &value),
                "Cannot create package compressor");
    }
    ~Compressor() {
        if (value)
            CloseCompressor(value);
    }
};
struct Decompressor {
    DECOMPRESSOR_HANDLE value = nullptr;
    Decompressor() {
        require(CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &value),
                "Cannot create package decompressor");
    }
    ~Decompressor() {
        if (value)
            CloseDecompressor(value);
    }
};
} // namespace
Bytes make_package(const std::map<std::string, Bytes> &files) {
    Bytes package = bytes("LFSETUP1");
    put32(package, unsigned(files.size()));
    Compressor compressor;
    for (const auto &[name, input] : files) {
        SIZE_T needed = 0;
        Compress(compressor.value, const_cast<std::uint8_t *>(input.data()), input.size(), nullptr, 0,
                 &needed);
        require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed, "Cannot size compressed package");
        Bytes compressed(needed);
        require(Compress(compressor.value, const_cast<std::uint8_t *>(input.data()), input.size(),
                         compressed.data(), compressed.size(), &needed),
                "Cannot compress package");
        compressed.resize(needed);
        put32(package, unsigned(name.size()));
        put64(package, input.size());
        put64(package, compressed.size());
        append(package, bytes(sha256(input)));
        append(package, bytes(name));
        append(package, compressed);
    }
    return package;
}
Package::Package(Bytes bytes_) : storage(std::move(bytes_)) {
    View data(storage);
    require(fixed_string(data, 0, 8) == "LFSETUP1", "Invalid setup package");
    auto count = u32(data, 8);
    require(count <= 256, "Too many package files");
    std::size_t p = 12;
    std::map<std::string, bool> names;
    for (unsigned i = 0; i < count; ++i) {
        auto length = u32(data, p);
        auto size = u64(data, p + 4), compressed = u64(data, p + 12);
        p += 20;
        require(length <= 512 && size <= 1024ull * 1024 * 1024 && compressed <= 1024ull * 1024 * 1024,
                "Invalid package file extent");
        auto hash = fixed_string(data, p, 64);
        p += 64;
        auto name = fixed_string(data, p, length);
        p += length;
        require(names.emplace(name, true).second, "Duplicate package file");
        validate_relative_path(name);
        auto content = slice(data, p, std::size_t(compressed));
        p += std::size_t(compressed);
        entries.push_back({name, hash, size, content});
    }
    require(p == data.size(), "Unexpected setup package bytes");
}
Package Package::embedded() {
    auto found = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
    require(found != nullptr, "The embedded setup package is missing");
    auto loaded = LoadResource(nullptr, found);
    auto pointer = static_cast<const std::uint8_t *>(LockResource(loaded));
    auto size = SizeofResource(nullptr, found);
    require(pointer && size, "Cannot read embedded setup package");
    return Package(Bytes(pointer, pointer + size));
}
Bytes Package::get(const std::string &name) const {
    auto entry =
        std::find_if(entries.begin(), entries.end(), [&](const auto &file) { return file.name == name; });
    require(entry != entries.end(), "Missing setup package file: " + name);
    Decompressor decompressor;
    Bytes result(std::size_t(entry->size));
    SIZE_T actual = 0;
    require(Decompress(decompressor.value, const_cast<std::uint8_t *>(entry->compressed.data()),
                       entry->compressed.size(), result.data(), result.size(), &actual) &&
                actual == result.size(),
            "Cannot extract setup package file");
    require(sha256(result) == entry->sha256, "Setup package file failed verification");
    return result;
}
} // namespace lifeline::setup
