#include "speech/recording.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace lifeline {
std::string speech_recording_levels(std::span<const std::int16_t> pcm){
    double sum=0;int peak=0;unsigned clipped=0;
    for(auto s:pcm){sum+=double(s)*s;peak=std::max(peak,std::abs(int(s)));clipped+=std::abs(int(s))>=32760;}
    std::ostringstream out;out<<pcm.size()*1000/16000<<" ms | RMS "<<std::fixed<<std::setprecision(1)<<20*std::log10(std::max(1.,std::sqrt(sum/std::max(std::size_t(1),pcm.size())))/32768.)
        <<" dBFS | peak "<<20*std::log10(std::max(1,peak)/32768.)<<" dBFS | clipped "<<clipped;
    return out.str();
}
void save_speech_recording(const std::filesystem::path& path,std::span<const std::int16_t> pcm){
    if(pcm.empty() || pcm.size()>160000)throw std::runtime_error("Invalid microphone recording length");
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot save microphone recording");
    const auto u16=[&](unsigned n){out.put(char(n&255));out.put(char((n>>8)&255));};
    const auto u32=[&](unsigned n){u16(n&65535);u16(n>>16);};
    out.write("RIFF",4);u32(36+unsigned(pcm.size())*2);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(16000);u32(32000);u16(2);u16(16);
    out.write("data",4);u32(unsigned(pcm.size())*2);for(auto s:pcm)u16(static_cast<unsigned short>(s));
    out.flush();if(!out)throw std::runtime_error("Microphone recording write failed");
}
std::filesystem::path save_speech_report(const std::filesystem::path& directory,std::span<const std::int16_t> pcm,const std::string& report){
    static std::atomic<unsigned> serial=0;
    std::filesystem::create_directories(directory);
    const auto now=std::chrono::system_clock::now().time_since_epoch().count();
    const auto stem=directory/("speech-"+std::to_string(now)+"-"+std::to_string(++serial));
    auto wav=stem;wav+=".wav";save_speech_recording(wav,pcm);
    auto txt=stem;txt+=".txt";std::ofstream log(txt);
    log<<"16 kHz mono signed 16-bit PCM, before recognition conditioning\n"<<speech_recording_levels(pcm)<<'\n'<<report<<'\n';
    log.flush();if(!log)throw std::runtime_error("Diagnostic text write failed");return wav;
}
}
