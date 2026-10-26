#include "speech/normalization.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lifeline {
SpeechNormalizationData read_speech_normalization(std::span<const std::byte> bytes) {
    std::size_t offset=0;
    const auto integer=[&]() {
        if(bytes.size()-offset<4) throw std::invalid_argument("Truncated speech normalization file");
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i) value|=std::uint32_t(std::to_integer<unsigned>(bytes[offset++]))<<(8*i);
        return value;
    };
    if(integer()!=0x314e534cu || integer()!=1)
        throw std::invalid_argument("Unsupported speech normalization file");
    const auto bands=integer(),columns=integer();
    if(bands!=21 || columns!=10 || bytes.size()!=16+4*(2*bands*columns+columns+2*bands+353))
        throw std::invalid_argument("Invalid speech normalization file layout");
    const auto vector=[&](unsigned count) {
        std::vector<float> result(count);
        for(auto& value:result) {
            value=std::bit_cast<float>(integer());
            if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite speech normalization file value");
        }
        return result;
    };
    SpeechNormalizationData result;
    result.projection={bands,columns,vector(bands*columns),vector(bands*columns),vector(columns)};
    result.initial_baseline=vector(bands); result.band_floor=vector(bands); result.soft_floor=vector(353);
    return result;
}
}
