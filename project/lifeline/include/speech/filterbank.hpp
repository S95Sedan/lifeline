#pragma once
#include <span>
#include <vector>

namespace lifeline {
// 0x274ce8 / 0x275530. Warp is supplied by the graph activation event;
// choosing the active warp/branch belongs to the recognition session.
class SpeechFilterbank {
    struct Band {unsigned first;std::vector<float> weights;};
    unsigned bins_;
    std::vector<Band> bands_;
public:
    SpeechFilterbank(unsigned bins,unsigned bands,float maximum_frequency,float warp,unsigned discard=0);
    std::vector<float> transform(std::span<const float> power)const;
};
}
