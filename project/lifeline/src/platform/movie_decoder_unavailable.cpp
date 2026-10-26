#include "media/movie_decoder.hpp"
#include <stdexcept>
namespace lifeline {
struct MovieDecoder::Impl {};
MovieDecoder::MovieDecoder(const std::filesystem::path&,bool,unsigned){throw std::runtime_error("Native MP4 decoding is not implemented for this platform yet");}
MovieDecoder::MovieDecoder(const std::filesystem::path&,MovieDecodeMode,unsigned){throw std::runtime_error("Native MP4 decoding is not implemented for this platform yet");}
MovieDecoder::~MovieDecoder()=default;
void MovieDecoder::seek(unsigned){}
std::optional<MovieSample> MovieDecoder::read(){return {};}
}
