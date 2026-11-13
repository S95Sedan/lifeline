#include "media/music.hpp"
#include "media/envelope.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <SDL3/SDL.h>
#include <cstring>
#include <optional>
namespace lifeline {
namespace {
using audio_detail::Envelope;
struct Sample {std::vector<float> pcm;std::size_t loop=0;bool repeating=false;};
struct Tone {unsigned low,high,root,sample,volume,pan,bend,flags,adsr1,adsr2;int fine;};
struct Program {unsigned volume,pan,bend;bool layered;std::vector<Tone> tones;};
struct Channel {unsigned program=0,volume=100,pan=64,bend=64;};
struct Event {std::uint64_t frame;unsigned command,a=0,b=0;};
struct Voice {unsigned channel,key,velocity;const Tone* tone;const Program* program;const Sample* sample;Envelope envelope;double position=0;};
}
struct Music::Impl {
    std::vector<Program> programs;std::vector<Sample> samples;std::vector<Event> events;
    std::array<Channel,16> initial{},channels{};std::array<unsigned,128> velocity{};
    std::vector<Voice> voices;std::uint64_t position=0,loops=0,length=0;std::size_t event=0;
    unsigned sequence_volume=0;
    bool authored=false,ended=false;
    std::optional<std::size_t> loop_event;
    std::uint64_t loop_frame=0;
    unsigned repetitions=0,repeat_count=0;
    Impl(const AssetDirectory& assets,const std::string& path){
        const auto data=assets.read(path);std::istringstream input(std::string(data.begin(),data.end()));std::string line;
        if(!std::getline(input,line) || (line!="LIFELINE_MUSIC\t1" && line!="LIFELINE_MUSIC\t2"))throw std::runtime_error("Unsupported installed music format");
        authored=line=="LIFELINE_MUSIC\t2";
        bool have_loop=false,have_velocity=false;unsigned channel_count=0;
        while(std::getline(input,line)){
            std::istringstream row(line);std::string kind;row>>kind;
            if(kind==(authored?"length":"loop")){
                if(have_loop || !(row>>length>>sequence_volume) || !length || length>48000ull*60*20 || sequence_volume>127)throw std::runtime_error("Invalid installed music loop");
                have_loop=true;
            }else if(kind=="velocity"){
                if(have_velocity)throw std::runtime_error("Duplicate music velocity map");
                for(auto& v:velocity)if(!(row>>v) || v>127)throw std::runtime_error("Invalid music velocity map");have_velocity=true;
            }else if(kind=="channel"){
                unsigned id;Channel c;
                if(!(row>>id>>c.program>>c.volume>>c.pan>>c.bend) || id!=channel_count || id>=16 || c.program>255 || c.volume>127 || c.pan>127 || c.bend>127)throw std::runtime_error("Invalid music channel");
                initial[id]=c;++channel_count;
            }else if(kind=="program"){
                unsigned id,layered;Program p{};
                if(!(row>>id>>p.volume>>p.pan>>p.bend>>layered) || id!=programs.size() || id>=128 || p.volume>127 || p.pan>127 || p.bend>127 || layered>1)throw std::runtime_error("Invalid music program");
                p.layered=layered!=0;programs.push_back(std::move(p));
            }else if(kind=="sample"){
                unsigned id,repeating;std::string filename;Sample sample;
                if(!(row>>id>>filename>>sample.loop>>repeating) || id!=samples.size() || id>=4096 || repeating>1 || filename.find("..")!=std::string::npos || filename.find_first_of("\\:")!=std::string::npos || filename.front()=='/')throw std::runtime_error("Invalid music sample");
                const auto full=(std::filesystem::path(path).parent_path()/filename).generic_string();
                SDL_AudioSpec spec{};Uint8* bytes=nullptr;Uint32 size=0;
                if(!SDL_LoadWAV(assets.resolve(full).string().c_str(),&spec,&bytes,&size))throw std::runtime_error(SDL_GetError());
                const std::unique_ptr<Uint8,decltype(&SDL_free)> owned(bytes,SDL_free);
                if(spec.format!=SDL_AUDIO_S16LE || spec.channels!=1 || spec.freq!=48000 || !size || size%2 || size>48000*2*120)throw std::runtime_error("Invalid installed music PCM");
                sample.pcm.reserve(size/2);
                for(unsigned i=0;i<size;i+=2){const unsigned v=unsigned(bytes[i])|(unsigned(bytes[i+1])<<8);sample.pcm.push_back((v<32768?int(v):int(v)-65536)/32768.f);}
                if(sample.loop>=sample.pcm.size())throw std::runtime_error("Music loop outside sample");
                sample.repeating=repeating!=0;samples.push_back(std::move(sample));
            }else if(kind=="tone"){
                unsigned id;Tone t{};
                if(!(row>>id>>t.low>>t.high>>t.root>>t.sample>>t.volume>>t.pan>>t.bend>>t.flags>>t.adsr1>>t.adsr2>>t.fine) || id>=programs.size() || t.low>t.high || t.high>127 || t.root>127 || t.volume>127 || t.pan>127 || t.bend>127 || t.flags>255 || (t.flags&0x60) || t.adsr1>65535 || t.adsr2>65535 || t.fine< -128 || t.fine>127 || t.sample>=samples.size())throw std::runtime_error("Invalid music tone");
                if(programs[id].tones.size()>=128)throw std::runtime_error("Too many music tones");programs[id].tones.push_back(t);
            }else if(kind=="event"){
                Event e{};
                if(!(row>>e.frame>>e.command>>e.a>>e.b) || e.command>255 || e.a>127 || e.b>127 || events.size()>=100000 || (!events.empty() && e.frame<events.back().frame))throw std::runtime_error("Invalid music event");
                const auto k=e.command&0xf0;
                if(k!=0x80 && k!=0x90 && k!=0xb0 && k!=0xc0 && k!=0xe0)throw std::runtime_error("Unsupported music event");
                if(k==0xb0 && e.a!=6 && e.a!=7 && e.a!=10 && e.a!=99 && !(authored && (e.a==1 || e.a==2)))throw std::runtime_error("Unsupported music controller");events.push_back(e);
            }else throw std::runtime_error("Unknown installed music record");
            std::string extra;if(row>>extra)throw std::runtime_error("Extra installed music fields");
        }
        if(!have_loop || !have_velocity || channel_count!=16 || programs.empty() || samples.empty() || events.empty() || events.back().frame>length)throw std::runtime_error("Incomplete installed music");
        // Inactive channels can begin with no program (255). Program changes
        // and note events validate the selected instrument when it is used.
        if(!authored)for(const auto& p:programs)if(p.tones.empty())throw std::runtime_error("Empty music program");
        channels=initial;voices.reserve(64);
    }
    void apply(const Event& e){
        const auto c=e.command&15,kind=e.command&0xf0;auto& channel=channels[c];
        if(kind==0xc0){if(!authored && e.a>=programs.size())throw std::runtime_error("Missing music program");channel.program=e.a;if(authored)channel.bend=64;}
        else if(kind==0xb0){
            if(e.a==10)channel.pan=e.b;else if(e.a==7)channel.volume=e.b;
            else if(authored && e.a==1 && e.b)throw std::runtime_error("Music modulation depth requires native LFO");
            else if(authored && e.a==6)repeat_count=e.b;
            else if(authored && e.a==99){
                if(e.b==20){loop_event=event;loop_frame=e.frame;}
                else if(e.b==30){
                    if(repeat_count==127 || repetitions<repeat_count){
                        if(!loop_event || e.frame<=loop_frame)throw std::runtime_error("Invalid authored music loop");
                        ++repetitions;++loops;position-=e.frame-loop_frame;event=*loop_event;
                    }else{loop_event.reset();repetitions=0;}
                }else throw std::runtime_error("Unsupported authored music controller selector");
            }
        }
        else if(kind==0xe0)channel.bend=e.a;
        else if(kind==0x80 || (kind==0x90 && !e.b)){for(auto& voice:voices)if(voice.channel==c && voice.key==e.a)voice.envelope.release();}
        else if(kind==0x90){
            if(channel.program>=programs.size())throw std::runtime_error("Missing music instrument");const auto& program=programs[channel.program];
            for(const auto& tone:program.tones)if(e.a>=tone.low && e.a<=tone.high){
                if(voices.size()>=128)throw std::runtime_error("Tutorial music voice limit exceeded");
                voices.push_back({c,e.a,e.b,&tone,&program,&samples[tone.sample],{tone.adsr1,tone.adsr2}});if(!program.layered)break;
            }
        }
    }
    void render(std::span<float> out){
        if(out.size()%2)throw std::runtime_error("Music output must be stereo");
        for(std::size_t f=0;f<out.size();f+=2){
            if(authored)while(!ended && event<events.size() && events[event].frame<=position)apply(events[event++]);
            if(position>=length && !ended){
                if(authored){ended=true;for(auto& voice:voices)voice.envelope.release();}
                else{position=0;event=0;channels=initial;++loops;}
            }
            while(!ended && event<events.size() && events[event].frame<=position)apply(events[event++]);
            float left=0,right=0;
            for(auto& voice:voices){
                const auto& sample=*voice.sample;const auto& tone=*voice.tone;const auto& program=*voice.program;const auto& channel=channels[voice.channel];
                if(voice.position>=sample.pcm.size()){if(sample.repeating)voice.position=sample.loop+std::fmod(voice.position-sample.loop,double(sample.pcm.size()-sample.loop));else {voice.envelope.phase=4;continue;}}
                if(voice.envelope.phase==4)continue;
                const auto i=std::size_t(voice.position);const auto next=i+1<sample.pcm.size()?i+1:sample.repeating?sample.loop:i;
                const auto pcm=sample.pcm[i]+(sample.pcm[next]-sample.pcm[i])*float(voice.position-i);
                const auto bend=(int(channel.bend)-64)/64.0*((tone.flags&8)?program.bend:tone.bend);
                voice.position+=std::exp2((int(voice.key)-int(tone.root)+tone.fine/16.0+bend)/12.0);
                const auto gain=voice.envelope.next()*(sequence_volume/127.f)*(channel.volume/127.f)*(program.volume/127.f)*(tone.volume/127.f)*(velocity[voice.velocity]/127.f);
                const auto pan=std::clamp(int(channel.pan)+int(program.pan)+int(tone.pan)-128,0,127)/127.f;
                left+=pcm*gain*std::min(1.f,2*(1-pan));right+=pcm*gain*std::min(1.f,2*pan);
            }
            std::erase_if(voices,[](const Voice& v){return v.envelope.phase==4;});
            out[f]=std::clamp(left,-1.f,1.f);out[f+1]=std::clamp(right,-1.f,1.f);++position;
        }
    }
};
Music::Music(const AssetDirectory& assets,const std::string& sequence):impl_(std::make_unique<Impl>(assets,sequence)){}
Music::~Music()=default;
void Music::render(std::span<float> out){impl_->render(out);}
std::uint64_t Music::loop_count()const{return impl_->loops;}
std::uint64_t Music::loop_frames()const{return impl_->length;}
unsigned Music::event_count()const{return unsigned(impl_->events.size());}
unsigned Music::sample_count()const{return unsigned(impl_->samples.size());}
bool Music::active()const{return !impl_->ended || !impl_->voices.empty();}
void Music::volume(unsigned value){if(value>127)throw std::runtime_error("Invalid music volume");impl_->sequence_volume=value;}
void Music::stop(){impl_->ended=true;impl_->voices.clear();}
}
