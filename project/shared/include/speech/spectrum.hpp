#pragma once
#include <complex>
#include <span>
#include <vector>

namespace lifeline {
struct SpeechSpectrumFrame {
    std::vector<float> power;
    float log_energy;
};
// Native Fourier implementation of the spectrum consumed by ASRSPI.
// Preserves its unnormalized power and energy-band convention; does not
// promise instruction-for-instruction rounding of the EE Hartley transform.
class SpeechSpectrum {
    unsigned size_,input_size_,first_,last_;
    float energy_floor_;
    std::vector<std::complex<float>> rotations_;
public:
    SpeechSpectrum(unsigned input_size,unsigned transform_size,unsigned first_energy_bin,
                   unsigned last_energy_bin,float energy_floor);
    SpeechSpectrumFrame transform(std::span<const float> window)const;
};
}
