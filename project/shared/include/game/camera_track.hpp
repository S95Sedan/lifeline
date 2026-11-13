#pragma once
#include "rendering/model.hpp"

namespace lifeline {
// Authored camera samples. The event controller supplies the frame position;
// file headers do not establish playback rate or whether a track should loop.
class CameraTrack {
    std::vector<SceneCamera> frames_;
    std::vector<bool> cut_;
    float step_=1;
public:
    explicit CameraTrack(std::span<const unsigned char> bytes);
    std::size_t size() const { return frames_.size(); }
    float step() const {return step_;}
    SceneCamera sample(double frame, bool loop=false) const;
    SceneCamera sample_authored(float frame) const;
};
}
