#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
namespace lifeline {
enum class MovieDecodeMode {audio_video,video_only,audio_only};
struct MovieSample {
    enum class Kind {video,audio};
    Kind kind;
    std::int64_t time_100ns=0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::int16_t> pcm;
};
// Standard installed MP4, decoded by the host platform. No disc or PS2 formats.
class MovieDecoder {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    MovieDecoder(const std::filesystem::path&,bool video_only=false,unsigned audio_priming=1024);
    MovieDecoder(const std::filesystem::path&,MovieDecodeMode,unsigned audio_priming=1024);
    ~MovieDecoder();
    void seek(unsigned frame);
    std::optional<MovieSample> read();
};
}
