#include "script/movies.hpp"
#include "script/movie_control.hpp"
#include "media/audio_settings.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <utility>

namespace lifeline::script {
void Movies::pause(bool paused){paused_=paused;if(paused_)pending_skip_generation_=0;for(auto& stream:streams_)if(stream.playback)stream.playback->pause(paused_||stream.paused);}
namespace {
std::atomic<std::uint64_t> next_fullscreen_generation{0};
std::uint64_t fullscreen_identity(){
    std::uint64_t value;do{value=next_fullscreen_generation.fetch_add(1,std::memory_order_relaxed)+1;}while(!value);return value;
}
int integer(float value){if(!std::isfinite(value) || double(value)<INT32_MIN || double(value)>INT32_MAX)throw std::runtime_error("Invalid movie argument");return int(std::nearbyint(value));}
std::string movie_name(const Invocation& c,unsigned argument){
    const int address=integer(c.arguments[argument]);if(address<0)throw std::runtime_error("Invalid movie name address");
    std::string path;
    for(unsigned word=unsigned(address);; ++word){
        if(c.state?!c.state->data_contains(c.program,word):word>=c.program.size())throw std::runtime_error("Unterminated movie name");
        auto packed=word<c.program.size()?c.program.word(word):0u;
        if(c.state){const auto i=c.state->data->find(word);if(i!=c.state->data->end())packed=std::bit_cast<unsigned>(i->second);}
        for(unsigned i=0;i<4;++i){const char ch=char(packed>>(8*i));if(!ch){
            const auto slash=path.find_last_of("/\\");if(slash!=std::string::npos)path.erase(0,slash+1);
            if(!path.ends_with(".is"))throw std::runtime_error("Invalid script movie extension");path.resize(path.size()-3);return path;
        }path+=ch;}
    }
}
void embedded_video_scanlines(AssetImage& image){
    // Disc/monitor playback is a script texture, not a full-screen movie.
    // Modulate its decoded source rows so the effect stays inside the display
    // even when the script changes the screen rectangle or its UVs.
    if(!image.width||!image.height||image.rgba.size()!=std::size_t(image.width)*image.height*4)return;
    for(unsigned y=1;y<image.height;y+=2){
        auto* row=image.rgba.data()+std::size_t(y)*image.width*4;
        for(unsigned x=0;x<image.width;++x)for(unsigned channel=0;channel<3;++channel)
            row[x*4+channel]=std::uint8_t((unsigned(row[x*4+channel])*239+127)/255);
    }
}
}
Movies::~Movies(){for(unsigned i=0;i<streams_.size();++i)stop(i);}
void Movies::close_audio_channel(int channel){
    for(auto& stream:streams_)if(stream.audio==channel){
        if(stream.playback)stream.playback->stop_audio();
        stream.audio=-1;
    }
}
void Movies::start(unsigned index,const std::string& name,bool loop){
    auto& s=streams_.at(index);if(s.state || (index && s.owned))return;
    s.name=name;s.loop=loop;s.state=1;s.paused=false;s.owned=true;s.decoded=0;s.loop_frames=0;s.section=0;
    if(index==0){
        scaled_frame_=0;pending_skip_generation_=0;player_skipped_=false;
        fullscreen_generation_=fullscreen_identity();
    }
}
void Movies::stop(unsigned index){
    auto& s=streams_.at(index);if(s.skipping)debug_completed_=true;s.skipping=false;s.playback.reset();if(s.audio>=0 && release_audio_)release_audio_(s.audio);
    s.audio=-1;s.state=0;s.owned=false;for(auto& buffer:s.buffers)buffer.rgba.clear();
    if(index==0){fullscreen_generation_=0;pending_skip_generation_=0;}
}
void Movies::complete(unsigned index,bool player_skip){
    // EOF retains ownership until the authored stop. Player skip has exactly
    // the same cleanup, including one audio release and expired image handles.
    const bool owned=streams_.at(index).owned;stop(index);streams_[index].owned=owned;
    if(index==0)player_skipped_=player_skip;
}
bool Movies::fullscreen_skippable()const{
    const auto& s=streams_[0];return s.state==5&&draw_&&!paused_&&!s.paused&&!s.skipping;
}
unsigned Movies::fullscreen_frame()const{return streams_[0].state==5?streams_[0].decoded:0;}
bool Movies::request_skip_fullscreen(std::uint64_t expected_generation){
    if(!expected_generation||expected_generation!=fullscreen_generation_||pending_skip_generation_||!fullscreen_skippable())return false;
    pending_skip_generation_=expected_generation;return true;
}
unsigned Movies::handle(unsigned index)const{const auto& s=streams_.at(index);return s.state==5?1+index*2+((s.decoded-1)&1):0;}
const AssetImage& Movies::image(unsigned value)const{
    if(!value || value>streams_.size()*2)throw std::runtime_error("Invalid movie image handle");
    const auto& image=streams_[(value-1)/2].buffers[(value-1)%2];if(image.rgba.empty())throw std::runtime_error("Expired movie image handle");return image;
}
int Movies::property(unsigned index,int selector)const{
    const auto& s=streams_.at(index);if(selector>=0 && selector<6)return s.properties[selector];
    return selector==6?s.audio:-1;
}
Bindings Movies::bindings(){
    Bindings out;
    out.emplace(movie_skip_result_opcode,Binding{0,1,[this](const Invocation&){return Reply{false,std::exchange(player_skipped_,false)?1.f:0.f};}});
    out.emplace(808,Binding{1,0,[this](const Invocation& c){start(0,movie_name(c,0),false);return Reply{false,0.f};}});
    out.emplace(809,Binding{0,0,[this](const Invocation&){stop(0);return Reply{false,0.f};}});
    for(unsigned op:{810u,811u})out.emplace(op,Binding{0,0,[this,op](const Invocation&){auto& s=streams_[0];s.paused=op==811;if(s.paused)pending_skip_generation_=0;if(s.playback)s.playback->pause(paused_||s.paused);return Reply{false,0.f};}});
    out.emplace(812,Binding{0,0,[this](const Invocation&){return Reply{false,streams_[0].state==5?float(streams_[0].decoded):0.f};}});
    out.emplace(813,Binding{0,0,[this](const Invocation&){return streams_[0].state?Reply{true,{}}:Reply{false,0.f};}});
    out.emplace(814,Binding{1,0,[this](const Invocation& c){draw_=integer(c.arguments[0])!=0;if(!draw_)pending_skip_generation_=0;return Reply{false,0.f};}});
    out.emplace(815,Binding{0,0,[this](const Invocation&){return Reply{false,float(streams_[0].state)};}});
    out.emplace(816,Binding{1,0,[this](const Invocation& c){return Reply{false,float(property(0,integer(c.arguments[0])))};}});
    out.emplace(817,Binding{0,0,[this](const Invocation&){return Reply{false,float(handle(0))};}});
    out.emplace(818,Binding{3,0,[this](const Invocation& c){const int slot=integer(c.arguments[0]);if(slot>=0 && slot<16)start(unsigned(slot)+1,movie_name(c,2),integer(c.arguments[1])!=1);return Reply{false,0.f};}});
    out.emplace(819,Binding{1,0,[this](const Invocation& c){const int slot=integer(c.arguments[0]);if(slot<16)for(unsigned i=slot<0?1:unsigned(slot)+1;i<(slot<0?17:unsigned(slot)+2);++i)stop(i);return Reply{false,0.f};}});
    for(unsigned op:{820u,821u})out.emplace(op,Binding{1,0,[this,op](const Invocation& c){const int slot=integer(c.arguments[0]);if(slot>=0 && slot<16){auto& s=streams_[slot+1];s.paused=op==821;if(s.playback)s.playback->pause(paused_||s.paused);}return Reply{false,0.f};}});
    out.emplace(822,Binding{2,0,[this](const Invocation& c){const int slot=integer(c.arguments[0]),mode=integer(c.arguments[1]);bool wait=false;
        if(slot>=0 && slot<16){const auto& s=streams_[slot+1];if(s.owned)wait=mode==0?s.state!=0:mode==1 && !(s.state==5 && s.decoded);}
        return wait?Reply{true,{}}:Reply{false,0.f};}});
    out.emplace(823,Binding{2,0,[this](const Invocation& c){const int slot=integer(c.arguments[0]),selector=integer(c.arguments[1]);if(slot<0 || slot>=16)return Reply{false,0.f};
        const auto& s=streams_[slot+1];if(selector>=0 && selector<=6)return Reply{false,float(property(slot+1,selector))};
        if(selector==7)return Reply{false,float(s.state)};if(selector==8)return Reply{false,s.state==5?float(s.decoded):0.f};return Reply{};}});
    out.emplace(824,Binding{1,0,[this](const Invocation& c){const int slot=integer(c.arguments[0]);return Reply{false,slot>=0 && slot<16?float(handle(slot+1)):0.f};}});
    return out;
}
void Movies::advance(){
    // A request belongs only to this update. Recheck after ordinary playback
    // completion, so EOF wins and neither replacement nor changed eligibility
    // can carry a pending hold into a later movie/frame.
    const auto pending=std::exchange(pending_skip_generation_,0);
    for(unsigned index=0;index<streams_.size();++index){auto& s=streams_[index];
        if(s.state==1){
            s.skipping=index==0&&skip_next_;if(s.skipping)skip_next_=false;
            if(!s.loop&&!s.skipping){
                if(reserve_audio_)s.audio=reserve_audio_();else for(int candidate=0;candidate<4;++candidate){bool used=false;for(const auto& other:streams_)used|=other.audio==candidate;if(!used){s.audio=candidate;break;}}
            }
            try{s.playback=std::make_unique<MoviePlayback>(assets_,s.name,s.skipping,0,false,silent_||s.skipping,s.audio>=0,scripted_movie_gain);}
            catch(...){stop(index);throw;}
            s.playback->pause(paused_||s.paused);s.state=5;s.frames=s.playback->timeline_frames();
            const auto& image=s.playback->image();const auto channels=s.playback->audio_channels();s.properties={int(image.width),int(image.height),int(s.frames),30,channels?48000:0,int(channels)};
            if(!channels && s.audio>=0){if(release_audio_)release_audio_(s.audio);s.audio=-1;}
        }else if(s.playback && !s.playback->advance()){
            if(s.loop){s.loop_frames+=s.frames;s.playback=std::make_unique<MoviePlayback>(assets_,s.name,false,0,false,silent_,false);s.playback->pause(paused_||s.paused);}
            else {complete(index);continue;}
        }
        if(index==0&&s.playback){
            // Natural section changes invalidate an unfinished hold too. Check
            // this before the request, so it cannot target the next section.
            const auto section=s.playback->current_section();
            if(section!=s.section){s.section=section;fullscreen_generation_=fullscreen_identity();}
            if(pending&&pending==fullscreen_generation_&&fullscreen_skippable()){
                if(s.playback->skip_section()){
                    s.section=s.playback->current_section();fullscreen_generation_=fullscreen_identity();
                }else {complete(index,true);continue;}
            }
        }
        if(s.playback){
            const unsigned decoded=s.loop_frames+s.playback->current_frame()+1;
            if(decoded!=s.decoded){
                s.decoded=decoded;
                auto& picture=s.buffers[(s.decoded-1)&1];picture=s.playback->image();
                if(index!=0)embedded_video_scanlines(picture);
            }
        }
    }
}
void Movies::render(AssetImage& destination)const{
    if(!draw_ || streams_[0].state!=5)return;
    const auto& source=image(handle(0));
    if(destination.rgba.size()!=std::size_t(destination.width)*destination.height*4)throw std::runtime_error("Invalid movie destination");
    if(scaled_frame_!=streams_[0].decoded||scaled_.width!=destination.width||scaled_.height!=destination.height){
        scaled_.width=destination.width;scaled_.height=destination.height;
        scaled_.rgba.resize(destination.rgba.size());
        render_movie(source,streams_[0].playback->aperture(),scaled_);
        scaled_frame_=streams_[0].decoded;
    }
    destination.rgba=scaled_.rgba;
}
}
