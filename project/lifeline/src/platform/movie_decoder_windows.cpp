#include "media/movie_decoder.hpp"
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <sstream>
namespace lifeline {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result) {
    if(SUCCEEDED(result))return;
    std::ostringstream text;text<<"Windows movie decoder failed (0x"<<std::hex<<unsigned(result)<<")";
    throw std::runtime_error(text.str());
}
struct MediaSession {
    bool com=false;
    MediaSession(){const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(result!=RPC_E_CHANGED_MODE)checked(result);com=SUCCEEDED(result);const auto mf=MFStartup(MF_VERSION);if(FAILED(mf)){if(com)CoUninitialize();checked(mf);}}
    ~MediaSession(){MFShutdown();if(com)CoUninitialize();}
};
struct BufferLock {
    IMFMediaBuffer* buffer;BYTE* bytes=nullptr;DWORD size=0;
    explicit BufferLock(IMFMediaBuffer* b):buffer(b){checked(buffer->Lock(&bytes,nullptr,&size));}
    ~BufferLock(){buffer->Unlock();}
};
}
struct MovieDecoder::Impl {
    MediaSession session;ComPtr<IMFSourceReader> reader;
    DWORD video=~0u,audio=~0u;bool video_only=false,video_end=false,audio_end=false;
    LONG stride=640*4;
    unsigned width=0,height=0;
    unsigned skip_audio=0,audio_priming=0,seek_priming=0;
    std::int64_t audio_position=0,seek_audio_target=0;
    bool seek_audio_position=false,trim_audio=false,audio_only=false;
    Impl(const std::filesystem::path& path,MovieDecodeMode mode,unsigned delay):video_only(mode==MovieDecodeMode::video_only),video_end(mode==MovieDecodeMode::audio_only),audio_end(video_only),skip_audio(delay),audio_priming(delay),audio_only(mode==MovieDecodeMode::audio_only) {
        ComPtr<IMFAttributes> attributes;checked(MFCreateAttributes(&attributes,2));
        checked(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
        checked(MFCreateSourceReaderFromURL(path.c_str(),attributes.Get(),&reader));
        checked(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE));
        for(DWORD i=0;;++i) {
            ComPtr<IMFMediaType> type;const auto result=reader->GetNativeMediaType(i,0,&type);
            if(result==MF_E_INVALIDSTREAMNUMBER)break;checked(result);
            GUID major{},subtype{};checked(type->GetGUID(MF_MT_MAJOR_TYPE,&major));checked(type->GetGUID(MF_MT_SUBTYPE,&subtype));
            if(major==MFMediaType_Video) {
                UINT32 num=0,den=0;
                checked(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width,&height));checked(MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&num,&den));
                if(video!=~0u || subtype!=MFVideoFormat_H264 || !width || !height || width>640 || height>448 || width%16 || height%16 || !den || num!=30*den)throw std::runtime_error("Unsupported installed movie video");
                video=i;
            }else if(major==MFMediaType_Audio) {
                UINT32 hz=0,channels=0;checked(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&hz));checked(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels));
                if(audio!=~0u || subtype!=MFAudioFormat_AAC || hz!=48000 || channels!=2)throw std::runtime_error("Unsupported installed movie audio");
                audio=i;
            }else throw std::runtime_error("Unexpected installed movie stream");
        }
        if(video==~0u)throw std::runtime_error("Missing installed movie video");
        if(audio==~0u)audio_end=true;
        if(!video_end){
        ComPtr<IMFMediaType> video_type;checked(MFCreateMediaType(&video_type));
        checked(video_type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));checked(video_type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
        checked(reader->SetCurrentMediaType(video,nullptr,video_type.Get()));checked(reader->SetStreamSelection(video,TRUE));
        video_type.Reset();checked(reader->GetCurrentMediaType(video,&video_type));
        stride=LONG(MFGetAttributeUINT32(video_type.Get(),MF_MT_DEFAULT_STRIDE,width*4));
        }
        if(!video_only && audio!=~0u) {
            ComPtr<IMFMediaType> pcm;checked(MFCreateMediaType(&pcm));
            checked(pcm->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));checked(pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
            checked(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));checked(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));
            checked(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));checked(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4));checked(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,192000));
            checked(reader->SetCurrentMediaType(audio,nullptr,pcm.Get()));checked(reader->SetStreamSelection(audio,TRUE));
        }
    }
    void seek(unsigned frame) {
        // Decode one second of AAC/keyframe preroll, then trim against the
        // original PCM timeline. Capture-only seeking retains its old path.
        const auto preroll=video_only?frame:frame>30?frame-30:0;
        PROPVARIANT position{};position.vt=VT_I8;position.hVal.QuadPart=std::int64_t(preroll)*10000000/30;
        checked(reader->SetCurrentPosition(GUID_NULL,position));video_end=audio_only;audio_end=video_only || audio==~0u;
        if(!video_only){skip_audio=0;seek_audio_target=std::int64_t(frame)*1600;seek_audio_position=true;trim_audio=true;seek_priming=preroll?audio_priming:0;}
    }
    std::optional<MovieSample> read() {
        while(!video_end || !audio_end) {
            DWORD stream=0,flags=0;LONGLONG timestamp=0;ComPtr<IMFSample> sample;
            checked(reader->ReadSample(video_end?audio:audio_end?video:MF_SOURCE_READER_ANY_STREAM,0,&stream,&flags,&timestamp,&sample));
            if(flags&MF_SOURCE_READERF_ERROR)throw std::runtime_error("Installed movie decoding failed");
            if(flags&(MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED|MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))throw std::runtime_error("Installed movie format changed during playback");
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){if(stream==video)video_end=true;else audio_end=true;}
            if(!sample)continue;
            MovieSample result;result.kind=stream==video?MovieSample::Kind::video:MovieSample::Kind::audio;result.time_100ns=timestamp;
            ComPtr<IMFMediaBuffer> buffer;checked(sample->ConvertToContiguousBuffer(&buffer));
            if(stream==video) {
                result.rgba.resize(std::size_t(width)*height*4);
                const auto copy=[&](const BYTE* bytes,LONG pitch) {
                    if(std::abs(pitch)<width*4)throw std::runtime_error("Invalid decoded movie stride");
                    for(unsigned y=0;y<height;++y){const auto* source=bytes+std::ptrdiff_t(y)*pitch;auto* destination=result.rgba.data()+std::size_t(y)*width*4;
                        for(unsigned x=0;x<width;++x){destination[x*4]=source[x*4+2];destination[x*4+1]=source[x*4+1];destination[x*4+2]=source[x*4];destination[x*4+3]=255;}}
                };
                ComPtr<IMF2DBuffer> surface;
                if(SUCCEEDED(buffer.As(&surface))) {
                    BYTE* top=nullptr;LONG pitch=0;checked(surface->Lock2D(&top,&pitch));
                    try{copy(top,pitch);}catch(...){surface->Unlock2D();throw;}checked(surface->Unlock2D());
                }else {
                    BufferLock lock(buffer.Get());const auto pitch=std::abs(stride);
                    if(lock.size<std::uint64_t(pitch)*height)throw std::runtime_error("Truncated decoded movie picture");
                    copy(stride<0?lock.bytes+std::size_t(height-1)*pitch:lock.bytes,stride);
                }
            }else {
                BufferLock lock(buffer.Get());if(lock.size%4)throw std::runtime_error("Invalid decoded stereo audio");
                // The Windows AAC source reader exposes encoder priming instead
                // of applying the MP4 edit-list trim. The installation recipe
                // records that lead-in. Return the original zero-based timeline.
                if(seek_audio_position){
                    // Seeking restarts the Windows AAC decoder's lead-in as
                    // well as retaining the file's encoder priming. Account
                    // for both before discarding the decoded preroll.
                    audio_position=(timestamp*48000+5000000)/10000000-audio_priming-seek_priming;
                    seek_audio_position=false;
                }
                const auto count=unsigned(lock.size/4);
                const auto skip=trim_audio?unsigned(std::min<std::int64_t>(std::max<std::int64_t>(0,seek_audio_target-audio_position),count)):std::min(skip_audio,count);
                if(trim_audio)audio_position+=skip;else skip_audio-=skip;
                const auto size=lock.size-skip*4;if(!size)continue;
                result.pcm.resize(size/2);std::memcpy(result.pcm.data(),lock.bytes+skip*4,size);
                result.time_100ns=audio_position*10000000/48000;audio_position+=size/4;
            }
            return result;
        }
        return {};
    }
};
MovieDecoder::MovieDecoder(const std::filesystem::path& p,bool only,unsigned delay):MovieDecoder(p,only?MovieDecodeMode::video_only:MovieDecodeMode::audio_video,delay){}
MovieDecoder::MovieDecoder(const std::filesystem::path& p,MovieDecodeMode mode,unsigned delay):impl_(std::make_unique<Impl>(p,mode,delay)){}
MovieDecoder::~MovieDecoder()=default;
void MovieDecoder::seek(unsigned frame){impl_->seek(frame);}
std::optional<MovieSample> MovieDecoder::read(){return impl_->read();}
}
