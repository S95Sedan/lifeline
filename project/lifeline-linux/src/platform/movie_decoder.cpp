#include "media/movie_decoder.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <string>

namespace lifeline {
namespace {
void checked(int status,const char* action){
    if(status>=0)return;char error[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(status,error,sizeof(error));
    throw std::runtime_error(std::string("Movie ")+action+": "+error);
}
}
struct MovieDecoder::Impl {
    AVFormatContext* format=nullptr;
    AVCodecContext* video=nullptr;
    AVCodecContext* audio=nullptr;
    AVPacket* packet=nullptr;
    AVFrame* frame=nullptr;
    SwsContext* scaler=nullptr;
    SwrContext* resampler=nullptr;
    int video_stream=-1,audio_stream=-1;
    bool ended=false;
    std::int64_t audio_position=0,seek_target=0;
    std::deque<MovieSample> pending;
    void close()noexcept{
        sws_freeContext(scaler);swr_free(&resampler);av_frame_free(&frame);av_packet_free(&packet);
        avcodec_free_context(&video);avcodec_free_context(&audio);avformat_close_input(&format);
    }
    AVCodecContext* open(int index){
        const auto* parameters=format->streams[index]->codecpar;const auto* codec=avcodec_find_decoder(parameters->codec_id);
        if(!codec)throw std::runtime_error("Installed movie codec is unavailable");
        auto* context=avcodec_alloc_context3(codec);if(!context)throw std::bad_alloc();
        try{checked(avcodec_parameters_to_context(context,parameters),"parameters");context->thread_count=2;checked(avcodec_open2(context,codec,nullptr),"codec initialization");}
        catch(...){avcodec_free_context(&context);throw;}return context;
    }
    Impl(const std::filesystem::path& path,MovieDecodeMode mode){
        try{
            checked(avformat_open_input(&format,path.c_str(),nullptr,nullptr),"open");checked(avformat_find_stream_info(format,nullptr),"stream discovery");
            for(unsigned i=0;i<format->nb_streams;++i){
                const auto* p=format->streams[i]->codecpar;
                if(p->codec_type==AVMEDIA_TYPE_VIDEO){
                    const auto rate=av_guess_frame_rate(format,format->streams[i],nullptr);
                    if(video_stream>=0||p->codec_id!=AV_CODEC_ID_H264||p->width<=0||p->height<=0||p->width>640||p->height>448||p->width%16||p->height%16||rate.num!=30*rate.den||rate.den<=0)throw std::runtime_error("Unsupported installed movie video");
                    video_stream=int(i);
                }else if(p->codec_type==AVMEDIA_TYPE_AUDIO){
                    if(audio_stream>=0||p->codec_id!=AV_CODEC_ID_AAC||p->sample_rate!=48000||p->ch_layout.nb_channels!=2)throw std::runtime_error("Unsupported installed movie audio");
                    audio_stream=int(i);
                }else throw std::runtime_error("Unexpected installed movie stream");
            }
            if(video_stream<0)throw std::runtime_error("Missing installed movie video");
            if(mode!=MovieDecodeMode::audio_only)video=open(video_stream);
            if(mode!=MovieDecodeMode::video_only&&audio_stream>=0){
                audio=open(audio_stream);AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
                checked(swr_alloc_set_opts2(&resampler,&stereo,AV_SAMPLE_FMT_S16,48000,&audio->ch_layout,audio->sample_fmt,audio->sample_rate,0,nullptr),"audio conversion");
                checked(swr_init(resampler),"audio initialization");
            }
            packet=av_packet_alloc();frame=av_frame_alloc();if(!packet||!frame)throw std::bad_alloc();
        }catch(...){close();throw;}
    }
    ~Impl(){close();}
    void drain(AVCodecContext* context,int stream){
        if(!context)return;
        for(;;){
            const auto status=avcodec_receive_frame(context,frame);if(status==AVERROR(EAGAIN)||status==AVERROR_EOF)return;checked(status,"decode");
            MovieSample sample;sample.kind=context==video?MovieSample::Kind::video:MovieSample::Kind::audio;
            const auto timestamp=frame->best_effort_timestamp;
            sample.time_100ns=timestamp==AV_NOPTS_VALUE?audio_position*10000000/48000:av_rescale_q(timestamp,format->streams[stream]->time_base,AVRational{1,10000000});
            if(context==video){
                scaler=sws_getCachedContext(scaler,frame->width,frame->height,AVPixelFormat(frame->format),frame->width,frame->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
                if(!scaler)throw std::runtime_error("Cannot convert installed movie video");
                sample.rgba.resize(std::size_t(frame->width)*frame->height*4);
                std::uint8_t* target[]{sample.rgba.data()};const int stride[]{frame->width*4};
                const auto rows=sws_scale(scaler,frame->data,frame->linesize,0,frame->height,target,stride);
                if(rows!=frame->height)throw std::runtime_error("Incomplete installed movie picture");
            }else{
                sample.pcm.resize(std::size_t(swr_get_out_samples(resampler,frame->nb_samples))*2);
                auto* output=reinterpret_cast<std::uint8_t*>(sample.pcm.data());
                const auto count=swr_convert(resampler,&output,int(sample.pcm.size()/2),const_cast<const std::uint8_t**>(frame->extended_data),frame->nb_samples);checked(count,"audio conversion");
                sample.pcm.resize(std::size_t(count)*2);
                // FFmpeg applies MP4 edit lists and AAC skip-sample metadata.
                // Applying the Windows decoder's extra 1024-frame trim here
                // would remove valid audio and desynchronize the movie.
                auto first=av_rescale_q(sample.time_100ns,AVRational{1,10000000},AVRational{1,48000});
                const auto skip=std::size_t(std::clamp<std::int64_t>(seek_target-first,0,count));
                sample.pcm.erase(sample.pcm.begin(),sample.pcm.begin()+skip*2);first+=skip;
                sample.time_100ns=first*10000000/48000;audio_position=first+count-std::int64_t(skip);
            }
            av_frame_unref(frame);
            if(sample.kind==MovieSample::Kind::video||!sample.pcm.empty())pending.push_back(std::move(sample));
        }
    }
    void seek(unsigned picture){
        const auto timestamp=av_rescale_q(picture,AVRational{1,30},format->streams[video_stream]->time_base);
        checked(av_seek_frame(format,video_stream,timestamp,AVSEEK_FLAG_BACKWARD),"seek");
        if(video)avcodec_flush_buffers(video);if(audio){avcodec_flush_buffers(audio);swr_close(resampler);checked(swr_init(resampler),"seek audio");}
        ended=false;pending.clear();audio_position=seek_target=std::int64_t(picture)*1600;av_packet_unref(packet);
    }
    std::optional<MovieSample> read(){
        while(pending.empty()&&!ended){
            const auto status=av_read_frame(format,packet);
            if(status==AVERROR_EOF){
                if(video){checked(avcodec_send_packet(video,nullptr),"video flush");drain(video,video_stream);}
                if(audio){checked(avcodec_send_packet(audio,nullptr),"audio flush");drain(audio,audio_stream);}
                ended=true;break;
            }
            checked(status,"demux");auto* codec=packet->stream_index==video_stream?video:packet->stream_index==audio_stream?audio:nullptr;
            if(codec){checked(avcodec_send_packet(codec,packet),"packet");drain(codec,packet->stream_index);}av_packet_unref(packet);
        }
        if(pending.empty())return {};auto result=std::move(pending.front());pending.pop_front();return result;
    }
};
MovieDecoder::MovieDecoder(const std::filesystem::path& path,bool only,unsigned priming):MovieDecoder(path,only?MovieDecodeMode::video_only:MovieDecodeMode::audio_video,priming){}
MovieDecoder::MovieDecoder(const std::filesystem::path& path,MovieDecodeMode mode,unsigned):impl_(std::make_unique<Impl>(path,mode)){}
MovieDecoder::~MovieDecoder()=default;
void MovieDecoder::seek(unsigned frame){impl_->seek(frame);}
std::optional<MovieSample> MovieDecoder::read(){return impl_->read();}
}
