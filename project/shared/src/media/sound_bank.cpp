#include "media/sound_bank.hpp"
#include "media/audio_output.hpp"
#include "media/room_reverb.hpp"
#include "media/envelope.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <map>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <optional>
namespace lifeline {
namespace {
struct Sample {std::vector<float> pcm;unsigned loop=0;bool repeating=false;};
struct Tone {unsigned sample,root,a,b,volume,pan,flags,bend,wave;int fine;};
struct Program {unsigned volume,pan,bend;std::map<unsigned,Tone> tones;};
struct Event {enum Kind {note,jump,slide,depth,rate,volume,end} kind;unsigned a=0,b=0,c=0,d=0,delay=0;int amount=0;};
struct Sequence {std::vector<Event> events;};
struct Player {std::uint64_t id;unsigned group;const Sequence* sequence;float left,right;std::size_t event=0;std::uint64_t due=0;unsigned repeats=0;bool ended=false;};
struct Voice {
    std::uint64_t owner;unsigned group,program,key;const Sample* sample;audio_detail::Envelope envelope;
    double position=0,step=1;float left=0,right=0;
    float slide=0,slide_delta=0;unsigned slide_ticks=0;
    unsigned wave=0,depth=0,phase=0,rate=4,hold=0,bend=0;
    double vibrato=1;unsigned volume=127,volume_from=127,volume_to=127,volume_ticks=0,volume_total=0;
};
[[noreturn]] void invalid(){throw std::runtime_error("Invalid installed sound bank");}
double pitch(unsigned key,unsigned root,int fine){
    const int delta=int(key)-int(root);const unsigned octave=unsigned(std::abs(delta))/12,remainder=unsigned(std::abs(delta))%12;
    const auto semitone=delta>=0?remainder:12-remainder;
    const double table=std::round(4096*std::exp2((int(semitone)*16+fine)/192.0));
    const double shifted=delta>=0?std::ldexp(table,int(octave)):std::floor(std::ldexp(table,-int(octave)-1));
    return std::clamp(std::floor(shifted*441/480),1.0,16383.0)/4096;
}
}
struct SoundBank::Impl {
    std::vector<Sample> samples;std::map<unsigned,Program> programs;
    std::map<std::pair<unsigned,unsigned>,Sequence> sequences;
    std::map<unsigned,std::array<unsigned,60>> waves;
    std::array<unsigned,128> velocity{};unsigned master=0;
    std::vector<Player> players;std::vector<Voice> voices;std::uint64_t clock=0,next_id=1;
    Impl(const AssetDirectory& assets,const std::string& path){
        const auto bytes=assets.read(path);std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string line;
        if(!std::getline(input,line) || line!="LIFELINE_SOUND_BANK\t1")invalid();
        bool have_master=false,have_velocity=false;Sequence* sequence=nullptr;
        while(std::getline(input,line)){
            std::istringstream row(line);std::string kind;row>>kind;
            if(kind=="master"){if(have_master || !(row>>master) || master>127)invalid();have_master=true;}
            else if(kind=="velocity"){if(have_velocity)invalid();for(auto& v:velocity)if(!(row>>v) || v>127)invalid();have_velocity=true;}
            else if(kind=="lfo"){
                unsigned id;std::array<unsigned,60> values{};if(!(row>>id)||id>255)invalid();
                for(auto& v:values)if(!(row>>v)||v>255)invalid();
                if(!waves.emplace(id,values).second)invalid();
            }else if(kind=="program"){
                unsigned id;Program p{};if(!(row>>id>>p.volume>>p.pan>>p.bend) || id>127 || p.volume>127 || p.pan>127 || p.bend>127 || !programs.emplace(id,std::move(p)).second)invalid();
            }else if(kind=="sample"){
                unsigned id,repeat;std::string name;Sample sample;
                if(!(row>>id>>name>>sample.loop>>repeat) || id!=samples.size() || repeat>1 || samples.size()>=4096 || name.empty() || name.find("..")!=std::string::npos || name.find_first_of("\\:")!=std::string::npos || name.front()=='/')invalid();
                SDL_AudioSpec spec{};Uint8* data=nullptr;Uint32 size=0;
                if(!SDL_LoadWAV(assets.resolve((std::filesystem::path(path).parent_path()/name).generic_string()).string().c_str(),&spec,&data,&size))throw std::runtime_error(SDL_GetError());
                const std::unique_ptr<Uint8,decltype(&SDL_free)> owned(data,SDL_free);
                if(spec.freq!=48000 || spec.channels!=1 || spec.format!=SDL_AUDIO_S16LE || !size || size%2 || size>48000*2*120 || sample.loop>=size/2)invalid();
                for(unsigned i=0;i<size;i+=2){unsigned v=data[i]|unsigned(data[i+1])<<8;sample.pcm.push_back((v<32768?int(v):int(v)-65536)/32768.f);}
                sample.repeating=repeat!=0;samples.push_back(std::move(sample));
            }else if(kind=="tone"){
                unsigned program,key,vibrato;Tone t{};
                if(!(row>>program>>key>>t.sample>>t.root>>t.fine>>t.a>>t.b>>vibrato>>t.volume>>t.pan>>t.bend>>t.wave>>t.flags) || !programs.contains(program) || key>127 || t.root>127 || t.fine< -128 || t.fine>127 || t.sample>=samples.size() || t.a>65535 || t.b>65535 || t.volume>127 || t.pan>127 || vibrato>255 || t.bend>127 || t.wave>255 || t.flags>255 || !programs.at(program).tones.emplace(key,t).second)invalid();
            }else if(kind=="sequence"){
                unsigned group,id;if(sequence || !(row>>group>>id) || group>127 || id>4095)invalid();
                auto [i,inserted]=sequences.emplace(std::pair{group,id},Sequence{});if(!inserted)invalid();sequence=&i->second;
            }else {
                if(!sequence || sequence->events.size()>=10000)invalid();Event e{};
                if(kind=="note"){
                    e.kind=Event::note;if(!(row>>e.a>>e.b>>e.c>>e.delay) || e.a>127 || e.b>127 || !programs.contains(e.c) || !programs.at(e.c).tones.contains(e.a))invalid();
                }else if(kind=="jump"){
                    e.kind=Event::jump;if(!(row>>e.a>>e.b>>e.delay) || e.b>255)invalid();
                }else if(kind=="pitch_slide"){
                    e.kind=Event::slide;if(!(row>>e.a>>e.amount>>e.c>>e.d>>e.delay) || !e.a || e.a>255 || e.amount< -128 || e.amount>127 || !programs.contains(e.c) || !programs.at(e.c).tones.contains(e.d))invalid();
                }else if(kind=="vibrato_depth"||kind=="vibrato_rate"){
                    e.kind=kind=="vibrato_depth"?Event::depth:Event::rate;
                    if(!(row>>e.a>>e.c>>e.d>>e.delay)||e.a>127||!programs.contains(e.c)||!programs.at(e.c).tones.contains(e.d))invalid();
                }else if(kind=="volume_ramp"){
                    e.kind=Event::volume;
                    if(!(row>>e.a>>e.b>>e.c>>e.d>>e.delay)||e.a>63||e.b>127||!programs.contains(e.c)||!programs.at(e.c).tones.contains(e.d))invalid();
                }else if(kind=="end")e.kind=Event::end;
                else invalid();
                if(e.delay>0xfffffff)invalid();sequence->events.push_back(e);
                if(e.kind==Event::end)sequence=nullptr;
            }
            std::string extra;if(row>>extra)invalid();
        }
        if(sequence || !have_master || !have_velocity || samples.empty() || sequences.empty())invalid();
        for(const auto& [key,s]:sequences)for(const auto& e:s.events)if(e.kind==Event::jump && e.a>=s.events.size())invalid();
    }
    void advance(Player& player){
        unsigned budget=0;
        while(!player.ended && player.due<=clock){
            if(++budget>10000)throw std::runtime_error("Sound sequence contains a zero-time loop");
            const auto& e=player.sequence->events.at(player.event++);player.due+=std::uint64_t(e.delay)*100;
            if(e.kind==Event::end){player.ended=true;break;}
            if(e.kind==Event::jump){
                if(!e.b || player.repeats!=e.b){player.event=e.a;if(e.b)++player.repeats;}else player.repeats=0;
            }else if(e.kind==Event::slide){
                // Original CC65 targets live voices by sequence, program and key.
                for(auto& v:voices)if(v.owner==player.id && v.program==e.c && v.key==e.d){v.slide_ticks=e.a*4;v.slide_delta=float(e.amount)/v.slide_ticks;}
            }else if(e.kind==Event::depth||e.kind==Event::rate||e.kind==Event::volume){
                for(auto& v:voices)if(v.owner==player.id&&v.program==e.c&&v.key==e.d){
                    if(e.kind==Event::depth)v.depth=waves.contains(v.wave)?e.a:0;
                    else if(e.kind==Event::rate){v.rate=240/(60-e.a*58/127);v.hold=0;}
                    else {v.volume_from=v.volume;v.volume_to=e.b;v.volume_ticks=v.volume_total=e.a*4;if(!v.volume_ticks)v.volume=e.b;}
                }
            }else if(!e.b){
                // A0 velocity zero is scoped to the sound-bank group, not to
                // the individual cue instance. Authored start/stop cue pairs
                // deliberately key off a looping voice from a preceding
                // sequence in the same group (lift motors, machinery and boss
                // layers). Instance ownership remains separate for explicit
                // channel stops.
                for(auto& v:voices)if(v.group==player.group && v.program==e.c && v.key==e.a)v.envelope.release();
            }else {
                const auto& p=programs.at(e.c);const auto& t=p.tones.at(e.a);
                if(voices.size()>=128)throw std::runtime_error("Native sound voice capacity exceeded");
                const float gain=(master/127.f)*(p.volume/127.f)*(t.volume/127.f);
                const float pan=std::clamp(int(p.pan)+int(t.pan)-64,0,127)/127.f;
                voices.push_back({player.id,player.group,e.c,e.a,&samples[t.sample],{t.a,t.b},0,pitch(e.a,t.root,t.fine),gain*player.left*std::min(1.f,2*(1-pan)),gain*player.right*std::min(1.f,2*pan)});
                auto& voice=voices.back();voice.wave=t.wave;voice.bend=t.bend;voice.volume=voice.volume_from=voice.volume_to=velocity[e.b];
                voice.depth=(t.flags&32)&&waves.contains(t.wave)?127:0;
            }
        }
    }
    void render(std::span<float> out){
        if(out.size()%2)throw std::runtime_error("Sound output must be stereo");
        for(std::size_t f=0;f<out.size();f+=2,++clock){
            for(auto& p:players)advance(p);
            std::erase_if(players,[](const auto& p){return p.ended;});
            float left=0,right=0;
            for(auto& v:voices){
                const auto& s=*v.sample;
                if(v.position>=s.pcm.size()){
                    if(s.repeating)v.position=s.loop+std::fmod(v.position-s.loop,double(s.pcm.size()-s.loop));
                    else {v.envelope.phase=4;continue;}
                }
                if(clock%800==0 && v.slide_ticks){v.slide=std::clamp(v.slide+v.slide_delta,-480.f,480.f);--v.slide_ticks;}
                if(clock%800==0){
                    if(v.depth){
                        if(v.rate==120&&!v.hold)v.hold=6;
                        else {if(v.hold)--v.hold;v.phase+=v.rate;}
                        if(v.phase>=240)v.phase=v.rate/2;
                        const int bend=int(waves.at(v.wave)[v.phase/4]*v.depth/255)-int((v.depth+1)/2);
                        v.vibrato=std::exp2(double(bend)*v.bend/(64*12));
                    }else v.vibrato=1;
                    if(v.volume_ticks){
                        v.volume=unsigned(int(v.volume_to)+(int(v.volume_from)-int(v.volume_to))*int(v.volume_ticks)/int(v.volume_total));
                        if(!--v.volume_ticks)v.volume=v.volume_to;
                    }
                }
                const auto i=std::size_t(v.position),next=i+1<s.pcm.size()?i+1:s.repeating?s.loop:i;
                const float value=(s.pcm[i]+(s.pcm[next]-s.pcm[i])*float(v.position-i))*v.envelope.next();
                v.position+=v.step*std::exp2(v.slide/144.0)*v.vibrato;left+=value*v.left*(v.volume/127.f);right+=value*v.right*(v.volume/127.f);
            }
            std::erase_if(voices,[](const auto& v){return v.envelope.phase==4;});
            out[f]=std::clamp(left,-1.f,1.f);out[f+1]=std::clamp(right,-1.f,1.f);
        }
    }
};
SoundBank::SoundBank(const AssetDirectory& assets,const std::string& path):impl_(std::make_unique<Impl>(assets,path)){}
SoundBank::~SoundBank()=default;
std::uint64_t SoundBank::play(unsigned group,unsigned sequence,float left,float right){
    if(!std::isfinite(left) || !std::isfinite(right) || left<0 || right<0 || left>1 || right>1)throw std::runtime_error("Invalid sound gain");
    const auto found=impl_->sequences.find({group,sequence});if(found==impl_->sequences.end())throw std::runtime_error("Sound sequence is not installed");
    if(impl_->players.size()>=128)throw std::runtime_error("Too many active sound sequences");
    const auto id=impl_->next_id++;impl_->players.push_back({id,group,&found->second,left,right,0,impl_->clock});return id;
}
void SoundBank::stop(std::uint64_t id,bool release){std::erase_if(impl_->players,[id](const auto& p){return p.id==id;});if(release){for(auto& v:impl_->voices)if(v.owner==id)v.envelope.release();}else std::erase_if(impl_->voices,[id](const auto& v){return v.owner==id;});}
void SoundBank::stop_all(bool release){
    impl_->players.clear();
    if(release){for(auto& voice:impl_->voices)voice.envelope.release();}
    else impl_->voices.clear();
}
void SoundBank::render(std::span<float> out){impl_->render(out);apply_audio_output(out);}
bool SoundBank::active()const{return !impl_->players.empty() || !impl_->voices.empty();}
bool SoundBank::active(std::uint64_t id)const{
    return std::any_of(impl_->players.begin(),impl_->players.end(),[=](const auto& p){return p.id==id;})||
        std::any_of(impl_->voices.begin(),impl_->voices.end(),[=](const auto& v){return v.owner==id&&v.envelope.phase!=4;});
}
bool SoundBank::active(unsigned group,unsigned program,unsigned key)const{return std::any_of(impl_->voices.begin(),impl_->voices.end(),[=](const auto& voice){return voice.group==group&&voice.program==program&&voice.key==key&&voice.envelope.phase!=4;});}
bool SoundBank::contains(unsigned group,unsigned sequence)const{return impl_->sequences.contains({group,sequence});}
unsigned SoundBank::sequence_count()const{return unsigned(impl_->sequences.size());}
struct SoundPlayback::Impl {
    SoundBank bank;bool silent,paused=false;
    RoomReverb reverb;
    std::uint64_t rendered_frames=0;
    std::map<std::uint64_t,std::optional<std::uint64_t>> cues;
    AudioStream stream;
    Impl(const AssetDirectory& assets,const std::string& path,bool quiet):bank(assets,path),silent(quiet){}
    void advance(){
        if(paused)return;
        if(!bank.active()&&!reverb.active())return;
        if(silent){std::array<float,1600> block{};bank.render(block);reverb.process(block);std::erase_if(cues,[&](const auto& cue){return !bank.active(cue.first);});return;}
        const bool starting=!stream;
        if(starting){
            SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
            stream=open_output_stream(spec);
            if(!stream)throw std::runtime_error(SDL_GetError());
        }
        const int queued=SDL_GetAudioStreamQueued(stream.get());if(queued<0)throw std::runtime_error(SDL_GetError());
        const auto consumed=rendered_frames-std::min(rendered_frames,std::uint64_t(queued)/(2*sizeof(float)));
        std::erase_if(cues,[&](const auto& cue){return cue.second&&*cue.second<=consumed;});
        // Prime before starting; retain 300 ms so dense translucent-effect
        // views cannot starve the device while the software renderer works.
        constexpr int capacity=14400*2*sizeof(float);
        if(queued<capacity){
            std::array<float,28800> block{};const auto count=unsigned(capacity-queued)/sizeof(float)/2*2;
            bank.render(std::span(block).first(count));
            rendered_frames+=count/2;
            for(auto& [id,end]:cues)if(!end&&!bank.active(id))end=rendered_frames;
            reverb.process(std::span(block).first(count));
            apply_audio_output(std::span(block).first(count));
            if(!SDL_PutAudioStreamData(stream.get(),block.data(),int(count*sizeof(float))))throw std::runtime_error(SDL_GetError());
        }
        if(starting&&!SDL_ResumeAudioStreamDevice(stream.get()))throw std::runtime_error(SDL_GetError());
    }
};
SoundPlayback::SoundPlayback(const AssetDirectory& assets,const std::string& path,bool silent):impl_(std::make_unique<Impl>(assets,path,silent)){}
SoundPlayback::~SoundPlayback()=default;
std::uint64_t SoundPlayback::play(unsigned group,unsigned sequence){const auto id=impl_->bank.play(group,sequence);impl_->cues.emplace(id,std::nullopt);if(!impl_->silent)impl_->advance();return id;}
void SoundPlayback::stop(std::uint64_t id,bool release){
    impl_->bank.stop(id,release);
    if(!impl_->bank.active(id))if(auto cue=impl_->cues.find(id);cue!=impl_->cues.end())cue->second=impl_->rendered_frames;
}
void SoundPlayback::stop_all(bool release){
    impl_->bank.stop_all(release);
    if(!release)impl_->cues.clear();
    if(!release && impl_->stream && !SDL_ClearAudioStream(impl_->stream.get()))throw std::runtime_error(SDL_GetError());
}
bool SoundPlayback::active()const{return impl_->bank.active();}
bool SoundPlayback::cue_pending(std::uint64_t id)const{
    if(impl_->bank.active(id))return true;
    if(impl_->silent)return false;
    const auto cue=impl_->cues.find(id);if(cue==impl_->cues.end()||!impl_->stream)return false;
    if(!cue->second)return true;
    const int queued=SDL_GetAudioStreamQueued(impl_->stream.get());if(queued<0)throw std::runtime_error(SDL_GetError());
    const auto consumed=impl_->rendered_frames-std::min(impl_->rendered_frames,std::uint64_t(queued)/(2*sizeof(float)));
    return consumed<*cue->second;
}
bool SoundPlayback::active(unsigned group,unsigned program,unsigned key)const{return impl_->bank.active(group,program,key);}
bool SoundPlayback::contains(unsigned group,unsigned sequence)const{return impl_->bank.contains(group,sequence);}
void SoundPlayback::advance(){impl_->advance();}
void SoundPlayback::reverb_preset(unsigned mode){impl_->reverb.preset(mode);}
void SoundPlayback::reverb_depth(float depth){impl_->reverb.depth(depth);}
void SoundPlayback::pause(bool paused){
    if(impl_->stream){const bool ok=paused?SDL_PauseAudioStreamDevice(impl_->stream.get()):SDL_ResumeAudioStreamDevice(impl_->stream.get());if(!ok)throw std::runtime_error(SDL_GetError());}
    impl_->paused=paused;
}
}
