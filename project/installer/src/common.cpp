#include "common.hpp"
#include <bcrypt.h>
#include <shlobj.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lifeline::setup {
std::wstring wide(std::string_view text) {
    if (text.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    require(n > 0, "Invalid UTF-8 text");
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), n);
    return result;
}
std::string utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0,
                                nullptr, nullptr);
    require(n > 0, "Invalid Windows path");
    std::string result(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), n,
                        nullptr, nullptr);
    return result;
}
std::string path_text(const fs::path &path) { return utf8(path.wstring()); }
Bytes read_file(const fs::path &path) {
    auto size = fs::file_size(path);
    require(size <= 2ull * 1024 * 1024 * 1024, "File exceeds the supported buffer size");
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot open " + path_text(path));
    Bytes result(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char *>(result.data()), std::streamsize(size));
    require(input.gcount() == std::streamsize(size), "Truncated file: " + path_text(path));
    return result;
}
void write_file(const fs::path &path, View data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(bool(output), "Cannot write " + path_text(path));
    output.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    output.close();
    require(bool(output), "Could not finish writing " + path_text(path));
}
void write_text(const fs::path &path, std::string_view text) {
    write_file(path, View(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
}
class HashAlgorithm {
  public:
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    DWORD size = 0;
    HashAlgorithm() {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
                "Cannot open SHA256");
        DWORD used = 0;
        require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                  sizeof(size), &used, 0) >= 0,
                "SHA256 properties failed");
    }
    ~HashAlgorithm() { BCryptCloseAlgorithmProvider(algorithm, 0); }
};
class Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    Bytes object;

  public:
    Hash() {
        static HashAlgorithm provider;
        object.resize(provider.size);
        require(BCryptCreateHash(provider.algorithm, &handle, object.data(), provider.size, nullptr, 0, 0) >= 0,
                "Cannot create SHA256");
    }
    ~Hash() {
        if (handle)
            BCryptDestroyHash(handle);
    }
    void add(View data) {
        require(BCryptHashData(handle, const_cast<PUCHAR>(data.data()), ULONG(data.size()), 0) >= 0,
                "SHA256 failed");
    }
    std::string finish() {
        std::uint8_t digest[32];
        require(BCryptFinishHash(handle, digest, 32, 0) >= 0, "SHA256 finalization failed");
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (auto b : digest)
            result << std::setw(2) << unsigned(b);
        return result.str();
    }
};
std::string sha256(View data) {
    Hash hash;
    hash.add(data);
    return hash.finish();
}
std::string file_sha256(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot verify " + path_text(path));
    Hash hash;
    // Most resources are small; reuse one buffer instead of clearing a megabyte
    // for each of the thirty thousand files checked during setup.
    thread_local Bytes buffer(64 * 1024);
    while (input) {
        input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        if (input.gcount())
            hash.add(View(buffer.data(), std::size_t(input.gcount())));
    }
    require(input.eof(), "Cannot read " + path_text(path));
    return hash.finish();
}
Json read_json(const fs::path &path) {
    auto data = read_file(path);
    return Json::parse(data);
}
View slice(View data, std::size_t offset, std::size_t length) {
    require(offset <= data.size() && length <= data.size() - offset, "Source data extent is out of bounds");
    return data.subspan(offset, length);
}
std::uint32_t u32(View data, std::size_t p) {
    auto v = slice(data, p, 4);
    return v[0] | std::uint32_t(v[1]) << 8 | std::uint32_t(v[2]) << 16 | std::uint32_t(v[3]) << 24;
}
std::uint16_t u16(View data, std::size_t p) {
    auto v = slice(data, p, 2);
    return std::uint16_t(v[0] | unsigned(v[1]) << 8);
}
float f32(View data, std::size_t p) { return std::bit_cast<float>(u32(data, p)); }
std::string fixed_string(View data, std::size_t p, std::size_t n) {
    auto v = slice(data, p, n);
    auto end = std::find(v.begin(), v.end(), 0);
    return std::string(v.begin(), end);
}
void put16(Bytes &data, std::uint16_t v) {
    data.push_back(std::uint8_t(v));
    data.push_back(std::uint8_t(v >> 8));
}
void put32(Bytes &data, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        data.push_back(std::uint8_t(v >> (i * 8)));
}
void put64(Bytes &data, std::uint64_t v) {
    for (unsigned i = 0; i < 8; ++i)
        data.push_back(std::uint8_t(v >> (i * 8)));
}
void append(Bytes &dest, View source) { dest.insert(dest.end(), source.begin(), source.end()); }
Bytes bytes(std::string_view s) { return Bytes(s.begin(), s.end()); }
bool reparse(const fs::path &path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
fs::path validate_relative_path(std::string_view relative) {
    require(
        !relative.empty() && relative.find_first_of("\\:<>\"|?*") == std::string_view::npos &&
            std::none_of(relative.begin(), relative.end(), [](unsigned char value) { return value < 32; }),
        "Unsafe installed filename");
    fs::path name(wide(relative));
    require(!name.is_absolute() && !name.has_root_path(), "Absolute installed filename");
    for (const auto &part : name) {
        require(part != L".." && part != L"." && !part.empty() && part.native().back() != L'.' &&
                    part.native().back() != L' ',
                "Unsafe installed path component");
        auto base = part.native().substr(0, part.native().find(L'.'));
        std::transform(base.begin(), base.end(), base.begin(),
                       [](wchar_t value) { return wchar_t(towupper(value)); });
        require(base != L"CON" && base != L"PRN" && base != L"AUX" && base != L"NUL" &&
                    !(base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
                      base[3] >= L'1' && base[3] <= L'9'),
                "Reserved Windows installed filename");
    }
    return name;
}
fs::path safe_path(const fs::path &root, std::string_view relative) {
    const auto name = validate_relative_path(relative);
    auto path = root;
    for (const auto &part : name) {
        path /= part;
        require(!reparse(path), "Installed path contains a symbolic link or junction");
    }
    return path;
}
void check_tree(const fs::path &root) {
    if (!fs::exists(root))
        return;
    require(!reparse(root), "Installation contains a symbolic link or junction");
    for (const auto &entry : fs::recursive_directory_iterator(root))
        require(!reparse(entry.path()), "Installation contains a symbolic link or junction");
}
fs::path executable_path() {
    std::wstring buffer(32768, 0);
    auto n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    require(n && n < buffer.size(), "Cannot locate setup executable");
    buffer.resize(n);
    return fs::path(buffer);
}
fs::path default_destination() {
    PWSTR value = nullptr;
    require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value)),
            "Cannot locate the user Programs folder");
    fs::path result(value);
    CoTaskMemFree(value);
    return result / L"Programs/Lifeline";
}
fs::path find_iso() {
    for (const auto &root :
         {executable_path().parent_path(), executable_path().parent_path().parent_path(), fs::current_path()})
        for (const auto &name : {fs::path(L"sources/iso/Lifeline.iso"), fs::path(L"Lifeline.iso")})
            if (fs::is_regular_file(root / name))
                return fs::absolute(root / name);
    return {};
}
} // namespace lifeline::setup
