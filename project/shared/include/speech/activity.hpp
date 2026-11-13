#pragma once
#include <optional>
#include <vector>

namespace lifeline {
// Module 07: delayed sliding activity sum, 0x27f078.
class SpeechActivityGate {
    std::vector<float> history_;
    unsigned delay_, pending_=0, next_=0;
    float sum_=0, threshold_;
    bool finished_=false;
public:
    SpeechActivityGate(unsigned delay=5,unsigned window=15,float threshold=1);
    void reset();
    std::optional<bool> push(float activity);
    std::vector<bool> finish();
};
// Module 0c: centered energy stability window, 0x2825e0. Its output is
// delayed two frames; rapid energy changes reduce adaptation weight to 10%.
class SpeechAdaptationWeight {
    std::vector<float> energy_, weights_;
    unsigned radius_, pending_=0, energy_next_=0, weight_next_=0;
    float threshold_;
    bool finished_=false;
    float evaluate(float weight) const;
public:
    SpeechAdaptationWeight(unsigned radius=2,float threshold=2);
    void reset();
    std::optional<float> push(float energy,float weight);
    std::vector<float> finish();
};
}
