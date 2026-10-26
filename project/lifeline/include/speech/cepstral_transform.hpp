#pragma once
#include <span>
#include <vector>

namespace lifeline {
// Recovered 0x272fb8 / 0x273308 transform. Input: a separate energy value
// followed by the preceding stage's spectral-band values. No logarithm is
// invented here: compression/noise processing belongs to that preceding stage.
class OriginalSpeechCepstrum {
    unsigned bands_,coefficients_,discard_;
    float scale_;
    std::vector<float> matrix_;
public:
    OriginalSpeechCepstrum(unsigned bands,unsigned coefficients,unsigned lifter_period,unsigned discard);
    std::vector<float> transform(std::span<const float> energy_and_bands)const;
};
}
