#include "disc.hpp"
#include <set>

namespace lifeline::setup {
Bytes Disc::read(std::uint64_t offset, std::uint64_t size) {
    require(offset <= total && size <= total - offset && size <= 1024ull * 1024 * 1024,
            "ISO file extent is invalid");
    stream.clear();
    stream.seekg(std::streamoff(offset));
    Bytes result(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char *>(result.data()), std::streamsize(size));
    require(stream.gcount() == std::streamsize(size), "The source ISO is incomplete");
    return result;
}
Disc::Disc(const fs::path &iso) : stream(iso, std::ios::binary) {
    require(bool(stream), "Cannot open the Lifeline ISO");
    total = fs::file_size(iso);
    auto volume = read(16 * 2048, 2048);
    auto signature = bytes(std::string_view("\1CD001\1", 7));
    require(std::equal(signature.begin(), signature.end(), volume.begin()),
            "Expected an ISO 9660 disc image");
    auto size = u32(volume, 166);
    require(size <= 1024 * 1024, "ISO directory is too large");
    auto directory = read(std::uint64_t(u32(volume, 158)) * 2048, size);
    for (std::size_t p = 0; p < directory.size();) {
        auto n = directory[p];
        if (!n) {
            p = (p / 2048 + 1) * 2048;
            continue;
        }
        auto row = slice(directory, p, n);
        require(n >= 34 && 33u + row[32] <= n, "Invalid ISO directory record");
        if (row[33] != 0 && row[33] != 1) {
            std::string name(row.begin() + 33, row.begin() + 33 + row[32]);
            name = name.substr(0, name.find(';'));
            require(root.emplace(name, Extent{std::uint64_t(u32(row, 2)) * 2048, u32(row, 10)}).second,
                    "Duplicate ISO filename");
        }
        p += n;
    }
    require(root.contains("SLUS_208.48") && root.contains("OS.FAT") && root.contains("OS.SNP"),
            "Use the supported North American Lifeline disc (SLUS_208.48)");
    auto executable_extent = root.at("SLUS_208.48");
    require(executable_extent.size <= 32 * 1024 * 1024, "Unsupported executable extent");
    elf = read(executable_extent.offset, executable_extent.size);
    require(sha256(elf) == "328fd73a894138ff1cadae140d8615da3fc6c684f127c58be044b06d664b2dc6",
            "Unsupported Lifeline disc revision");
    auto fat_extent = root.at("OS.FAT");
    require(fat_extent.size <= 16 * 1024 * 1024, "Game directory is too large");
    auto fat = read(fat_extent.offset, fat_extent.size);
    require(fixed_string(fat, 0, 4) == " fat" && u32(fat, 4) == 0x10000, "Invalid game archive directory");
    payload = root.at("OS.SNP");
    require(payload.offset <= total && payload.size <= total - payload.offset, "Game data is truncated");
    auto count = u32(fat, 8);
    require(count && fat.size() == 128ull + 48ull * count, "Invalid archive directory length");
    std::set<unsigned> visited;
    std::function<void(unsigned, const std::string &, unsigned)> visit = [&](unsigned number,
                                                                             const std::string &parent,
                                                                             unsigned depth) {
        require(number < count && depth <= 32 && visited.insert(number).second,
                "Invalid archive directory links");
        auto p = 128ull + number * 48ull;
        auto name = fixed_string(fat, p, 16);
        require(!name.empty() && (number == 0 || (name != "." && name != ".." &&
                                                  name.find_first_of("/\\") == std::string::npos)),
                "Invalid archive filename");
        auto path = number ? parent + "/" + name : std::string{};
        auto flags = u32(fat, p + 32);
        auto length = u32(fat, p + 16);
        auto sector = u32(fat, p + 24);
        auto sectors = u32(fat, p + 28);
        if (flags == 0x10) {
            auto child = u32(fat, p + 44);
            while (child) {
                require(child % 48 == 0, "Misaligned archive link");
                visit(child / 48, path, depth + 1);
                child = u32(fat, 128ull + child + 40);
            }
        } else {
            require((flags == 0x20 || flags == 0x21) && length <= std::uint64_t(sectors) * 2048 &&
                        std::uint64_t(sector) * 2048 + length <= payload.size,
                    "Invalid game file extent");
            require(index.emplace(path, Extent{payload.offset + std::uint64_t(sector) * 2048, length}).second,
                    "Duplicate game archive path");
        }
    };
    visit(0, "", 0);
    require(visited.size() == count, "Unreachable archive directory record");
}
Bytes Disc::file(const std::string &key) {
    auto found = index.find(key);
    require(found != index.end(), "Missing disc resource: " + key);
    return read(found->second.offset, found->second.size);
}
Json Disc::identity() const {
    return Json{{"iso_bytes", total},
                {"elf_sha256", "328fd73a894138ff1cadae140d8615da3fc6c684f127c58be044b06d664b2dc6"}};
}
std::map<std::string, Bytes> archive_members(View data) {
    require(data.size() >= 16 && fixed_string(data, 0, 3) == "arc" && u32(data, 4) == 0x10000,
            "Unsupported game archive");
    auto count = u32(data, 8);
    std::size_t p = u32(data, 12);
    require(p <= data.size() && p % 128 == 0 && 16ull + count * 48ull <= p, "Invalid archive header");
    std::map<std::string, Bytes> result;
    for (unsigned i = 0; i < count; ++i) {
        auto offset = 16ull + i * 48ull;
        auto name = fixed_string(data, offset, 16);
        auto allocation = u32(data, offset + 16);
        auto size = u32(data, offset + 36);
        require(!name.empty() && name != "." && name != ".." &&
                    name.find_first_of("/\\") == std::string::npos && size <= allocation &&
                    allocation % 128 == 0,
                "Invalid archive member");
        auto content = slice(data, p, allocation);
        require(result.emplace(name, Bytes(content.begin(), content.begin() + size)).second,
                "Duplicate archive member");
        p += allocation;
    }
    require(p == data.size(), "Invalid archive payload length");
    return result;
}
} // namespace lifeline::setup
