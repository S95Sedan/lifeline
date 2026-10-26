#pragma once
#include "speech/acoustic_model.hpp"

namespace lifeline {
// Recovered feature-frame quantization and raw acoustic-row costs.
// Input is the original engine's feature vector, NOT PCM or arbitrary MFCCs.
class OriginalSpeechAcoustics {
    struct Stream {
        unsigned dimensions,coarse_count,candidates,fine_count,feature_offset,columns;
        std::vector<float> coarse,fine;
        std::vector<unsigned> shortlist;
        std::vector<std::uint8_t> labels,packed;
    };
    std::vector<Stream> streams_;
    std::vector<std::vector<std::uint16_t>> state_streams_;
    unsigned feature_count_=0;
public:
    explicit OriginalSpeechAcoustics(const OriginalSpeechData&);
    unsigned feature_count()const{return feature_count_;}
    std::vector<std::uint8_t> quantize(std::span<const float> features)const;
    // 0x273ab8 uses the static-cepstrum stream's nearest-centroid distance
    // to compare warp branches before temporal feature generation.
    float centroid_distance(unsigned stream,std::span<const float> coefficients)const;
    // RAW rows: do not index by model state ID. SpeechAcousticStates builds
    // the composite prefix and applies the model-ID offset.
    std::vector<std::uint16_t> state_costs(std::span<const std::uint8_t> codes)const;
};
}
