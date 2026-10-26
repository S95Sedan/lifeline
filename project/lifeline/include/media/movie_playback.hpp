#pragma once
#include "assets/asset_directory.hpp"
#include "rendering/image.hpp"
#include "media/movie_viewport.hpp"
#include <memory>

namespace lifeline {
// Shared native playback for script movies and the title/tutorial cinematics.
class MoviePlayback {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    MoviePlayback(const AssetDirectory&,const std::string&,bool capture=false,unsigned frame=0,bool mono=false,bool silent=false,bool audio=true,float gain=1.f);
    ~MoviePlayback();
    bool advance();
    void pause(bool);
    void stop_audio();
    const AssetImage& image()const;
    MovieAperture aperture()const;
    unsigned current_frame()const;
    unsigned timeline_frames()const;
    unsigned audio_channels()const;
};
}
