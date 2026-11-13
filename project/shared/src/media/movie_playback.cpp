#include "media/movie_playback.hpp"
#include "media/audio_output.hpp"
#include "media/movie_decoder.hpp"
#include "assets/installed_paths.hpp"
#include <SDL3/SDL.h>
#include <deque>
#include <sstream>
namespace lifeline {
namespace {void checked(bool ok){if(!ok)throw std::runtime_error(std::string("Movie SDL: ")+SDL_GetError());}}
struct MoviePlayback::Impl {
    std::unique_ptr<MovieDecoder> decoder;
    AssetImage image{"movie",0,0,{}};
    MovieAperture aperture;
    std::string name;
    AudioStream sound;
    struct Pending {unsigned index;std::vector<std::uint8_t> rgba;};
    std::deque<Pending> pictures;
    unsigned timeline=0,total_pictures=0,current=~0u,video_count=0;
    unsigned width=0,height=0,channels=0;
    static constexpr unsigned opening_story_frame=2445;
    bool opening_sections=false;
    unsigned seek_frame=0;
    std::uint64_t started=0;
    std::uint64_t samples=0,queued_samples=0,tail_start=0,duration_ms=0;
    bool capture=false,mono=false,eof=false,silent=false,paused=false;
    std::uint64_t pause_start=0,ticks=0;
    Impl(const AssetDirectory& assets,const std::string& name,bool quiet,unsigned initial,bool mix,bool mute,bool audio,float gain):name(name),capture(quiet),mono(mix),silent(mute) {
        if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw std::runtime_error("Invalid installed movie name");
        if(!capture && initial)throw std::runtime_error("Cinematic frame selection is capture-only");
        const auto bytes=assets.read(installed::movie(name,".txt"));std::istringstream meta(std::string(bytes.begin(),bytes.end()));
        std::string version,extra;unsigned rate=0,hz=0,priming=0;
        if(!(meta>>version>>width>>height>>rate>>timeline>>total_pictures>>hz>>channels>>samples>>priming) || meta>>extra || (version!="lifeline-movie-4" && version!="lifeline-movie-5") ||
            !width || !height || width>640 || height>448 || width%16 || height%16 || rate!=30 ||
            !((hz==48000 && channels==2 && samples && priming==1024) || (!hz && !channels && !samples && !priming)) || !total_pictures || total_pictures>timeline || timeline>100000 || samples>48000ULL*3600 || initial>=timeline)
            throw std::runtime_error("Invalid cinematic metadata");
        opening_sections=name=="s0"&&width==640&&height==448&&timeline==10447&&total_pictures==10406&&samples==16715776;
        const auto media=assets.resolve(installed::movie(name,".mp4"));
        aperture={0,0,width,height};
        auto aperture_path=media;aperture_path.replace_extension(".aperture");
        if(std::filesystem::exists(aperture_path)){
            std::ifstream input(aperture_path);unsigned w=0,h=0;std::string tag;
            if(!(input>>tag>>w>>h>>aperture.x>>aperture.y>>aperture.width>>aperture.height)||input>>extra||
               tag!="lifeline-aperture-1"||w!=width||h!=height||!aperture.width||!aperture.height||
               aperture.x>width||aperture.y>height||aperture.width>width-aperture.x||aperture.height>height-aperture.y)
                throw std::runtime_error("Invalid movie aperture metadata");
        }
        decoder=std::make_unique<MovieDecoder>(media,capture,priming);
        duration_ms=std::max((std::uint64_t(timeline)*1000+29)/30,(samples*1000+47999)/48000);
        image.width=width;image.height=height;
        if(capture) {
            const auto target=std::min(initial,total_pictures-1);if(target)decoder->seek(target);
            while(current!=target && !eof) {
                read_sample();
                while(!pictures.empty()) {
                    auto next=std::move(pictures.front());pictures.pop_front();
                    if(next.index>target)throw std::runtime_error("Cinematic seek skipped requested picture");
                    if(next.index==target){present(next);break;}
                }
            }
            if(current!=target)throw std::runtime_error("Cinematic picture missing");
        }else {
            if(samples && !silent && audio){checked(SDL_InitSubSystem(SDL_INIT_AUDIO));const SDL_AudioSpec spec{SDL_AUDIO_S16,2,48000};
                sound=open_audio_stream(AudioBus::fmv,spec,gain);}
            pump(0);display(0);started=SDL_GetTicks();if(sound)checked(SDL_ResumeAudioStreamDevice(sound.get()));
        }
    }
    void read_sample() {
        auto sample=decoder->read();
        if(!sample) {
            eof=true;
            if(!capture) {
                if(video_count!=total_pictures || queued_samples!=samples)throw std::runtime_error("Truncated cinematic streams");
                if(sound)checked(SDL_FlushAudioStream(sound.get()));
            }
            return;
        }
        if(sample->kind==MovieSample::Kind::video) {
            const auto index=(sample->time_100ns*30+5000000)/10000000;
            if(!capture&&index>=0&&index<seek_frame)return; // Decoder keyframe preroll.
            if(index<0 || index>=total_pictures || (!capture && index!=video_count) || sample->rgba.size()!=std::size_t(width)*height*4)throw std::runtime_error("Cinematic picture timeline changed");
            pictures.push_back({unsigned(index),std::move(sample->rgba)});++video_count;
            if(pictures.size()>90)throw std::runtime_error("Cinematic streams exceed bounded read-ahead");
        }else if(!capture) {
            // Decoder removes encoder priming. Clip trailing codec padding to
            // the original sample extent and preserve the stereo sample order.
            const auto first=(sample->time_100ns*48000+5000000)/10000000;
            const auto skip=std::size_t(std::max<std::int64_t>(0,-first));
            const auto count=sample->pcm.size()/2;
            if(skip>=count)return;
            const auto used=std::min(std::uint64_t(count-skip),samples-queued_samples);
            auto* pcm=sample->pcm.data()+skip*2;
            if(mono||mono_output.load(std::memory_order_relaxed))for(std::size_t i=0;i<used;++i)pcm[i*2]=pcm[i*2+1]=std::int16_t((int(pcm[i*2])+pcm[i*2+1])/2);
            if(used && sound)checked(SDL_PutAudioStreamData(sound.get(),pcm,int(used*4)));
            queued_samples+=used;
        }
    }
    void pump(std::uint64_t elapsed) {
        const auto ahead=elapsed+250;
        while(!eof && ((samples && queued_samples<samples && queued_samples*1000/48000<ahead) || (video_count<total_pictures && (pictures.empty() || std::uint64_t(pictures.back().index)*1000/30<ahead)) || (video_count==total_pictures && queued_samples==samples))) {
            read_sample();display(elapsed);
        }
    }
    void present(const Pending& next) {image.rgba=next.rgba;current=next.index;}
    void display(std::uint64_t elapsed) {
        const auto target=std::min(elapsed*30/1000,std::uint64_t(total_pictures-1));
        while(!pictures.empty() && pictures.front().index<=target) {
            auto next=std::move(pictures.front());pictures.pop_front();
            if(pictures.empty() || pictures.front().index>target)present(next);
        }
    }
    bool advance() {
        if(capture || paused)return true;
        if(silent){const auto elapsed=(++ticks)*1000/60;display(elapsed);pump(elapsed);display(elapsed);return ticks*30<std::uint64_t(timeline)*60 || ticks*48000<samples*60;}
        if(!sound){const auto elapsed=SDL_GetTicks()-started;display(elapsed);pump(elapsed);display(elapsed);return elapsed<duration_ms;}
        // Video follows consumed audio, not wall time. A decoder/render stall
        // must not advance the picture while the sound device has underrun.
        const auto queued=SDL_GetAudioStreamQueued(sound.get());checked(queued>=0);
        const auto remaining=std::min(queued_samples,std::uint64_t(queued)/4);
        auto elapsed=(queued_samples-remaining)*1000/48000;
        if(eof && !remaining){
            if(!tail_start)tail_start=SDL_GetTicks();
            elapsed=samples*1000/48000+SDL_GetTicks()-tail_start;
        }
        display(elapsed);pump(elapsed);display(elapsed);return elapsed<duration_ms;
    }
    void pause(bool value) {
        if(paused==value)return;
        const auto now=SDL_GetTicks();
        if(value){pause_start=now;if(sound)checked(SDL_PauseAudioStreamDevice(sound.get()));}
        else {const auto delay=now-pause_start;started+=delay;if(tail_start)tail_start+=delay;if(sound)checked(SDL_ResumeAudioStreamDevice(sound.get()));}
        paused=value;
    }
    unsigned section()const{return opening_sections&&current!=~0u&&current>=opening_story_frame?1:0;}
    bool skip_section(){
        if(capture||paused||!opening_sections||section())return false;
        if(sound){checked(SDL_PauseAudioStreamDevice(sound.get()));checked(SDL_ClearAudioStream(sound.get()));}
        decoder->seek(opening_story_frame);
        seek_frame=opening_story_frame;video_count=seek_frame;queued_samples=std::uint64_t(seek_frame)*1600;
        pictures.clear();eof=false;tail_start=0;ticks=std::uint64_t(seek_frame)*2;
        const auto elapsed=std::uint64_t(seek_frame)*1000/30;
        pump(elapsed);display(elapsed);
        if(current!=seek_frame)throw std::runtime_error("Opening section picture missing");
        started=SDL_GetTicks()-elapsed;
        if(sound)checked(SDL_ResumeAudioStreamDevice(sound.get()));
        return true;
    }
    void stop_audio(){
        if(!sound)return;
        const int queued=SDL_GetAudioStreamQueued(sound.get());checked(queued>=0);
        const auto remaining=std::min(queued_samples,std::uint64_t(queued)/4);
        auto elapsed=(queued_samples-remaining)*1000/48000;
        const auto now=paused?pause_start:SDL_GetTicks();
        if(tail_start)elapsed=samples*1000/48000+now-tail_start;
        // Preserve the consumed-audio position when switching clock sources.
        // Muting a movie must not jump to its decode/read-ahead position.
        started=now-elapsed;tail_start=0;sound.reset();
    }
};
MoviePlayback::MoviePlayback(const AssetDirectory& a,const std::string& n,bool c,unsigned f,bool m,bool s,bool audio,float gain):impl_(std::make_unique<Impl>(a,n,c,f,m,s,audio,gain)){}
MoviePlayback::~MoviePlayback()=default;
bool MoviePlayback::advance(){return impl_->advance();}
bool MoviePlayback::skip_section(){return impl_->skip_section();}
unsigned MoviePlayback::current_section()const{return impl_->section();}
void MoviePlayback::pause(bool value){impl_->pause(value);}
void MoviePlayback::stop_audio(){impl_->stop_audio();}
const AssetImage& MoviePlayback::image()const{return impl_->image;}
MovieAperture MoviePlayback::aperture()const{return fullscreen_movie_aperture(impl_->name,impl_->current,impl_->aperture);}
unsigned MoviePlayback::current_frame()const{return impl_->current;}
unsigned MoviePlayback::timeline_frames()const{return impl_->timeline;}
unsigned MoviePlayback::audio_channels()const{return impl_->channels;}
}
