#pragma once
#include <span>
#include <vector>

namespace lifeline {
// Column-major projection data extracted from the disc executable. Keeping the
// data separate also preserves the original precomputed floating-point values.
struct SpectralProjectionData {
    unsigned bands, columns;
    std::vector<float> basis, qr, reflectors;
};

// Recovered 0x275ab8: soft ceiling followed by a constrained spline projection.
class SpeechSpectralBaseline {
    SpectralProjectionData data_;
public:
    explicit SpeechSpectralBaseline(SpectralProjectionData data);
    std::vector<float> project(std::span<const float> bands) const;
};
}
