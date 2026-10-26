#pragma once
#include <array>
#include <vector>
#include <span>
namespace lifeline {
// Native stereo delay/filter network. No console audio device or memory map.
class RoomReverb {
    std::array<unsigned short,32> parameters_{};
    std::vector<float> delay_;
    unsigned cursor_=0,phase_=0,history_=0,pending_=0;
    std::array<std::array<float,2>,39> input_{},output_{};
    double energy_=0;
    float depth_=0;
    std::array<float,2> tick(float,float);
public:
    void preset(unsigned);
    void depth(float);
    void clear();
    std::array<float,2> process(float left,float right);
    void process(std::span<float> stereo);
    bool active()const{return !delay_.empty()&&depth_>0&&(energy_>1e-10||pending_);}
};
}
