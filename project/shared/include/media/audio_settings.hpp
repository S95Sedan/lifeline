#pragma once

namespace lifeline {
struct AudioVolumes {
    unsigned fmv=100,dialogue=100,music=100;
    bool operator==(const AudioVolumes&)const=default;
};
enum class AudioBus {fmv,dialogue,music};
inline constexpr float scripted_movie_gain=float(0x3998)/0x3fff;
}
