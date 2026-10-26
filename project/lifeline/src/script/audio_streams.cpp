#include "script/sound.hpp"
#include "media/sequence_playback.hpp"
#include "media/audio_output.hpp"
#include "script/memory.hpp"
#include <cmath>
#include <limits>
namespace lifeline::script {
namespace {
int number(float value){const auto n=std::nearbyint(value);if(!std::isfinite(n) || double(n)<std::numeric_limits<int>::min() || double(n)>std::numeric_limits<int>::max())throw std::runtime_error("Invalid streamed audio parameter");return int(n);}
std::string installed_path(const Resources& resources,std::string name){
    if(name.starts_with("./"))name.erase(0,1);
    // Direct streams in entry_099 use archive/ without a leading slash.
    if(name.starts_with("archive/"))name.insert(0,"/");
    if(!name.starts_with("/archive/") || !name.ends_with(".int") || name.find("..")!=std::string::npos || name.find_first_of("\\:\t\r\n")!=std::string::npos)throw std::runtime_error("Invalid authored stream path");
    const auto member=std::filesystem::path(name).filename().string();
    const auto& files=resources.installed_files(name,member);const ResourceFile* result=nullptr;
    for(const auto& file:files)if(file.conversion=="stream-wav-v1"){
        if(result)throw std::runtime_error("Ambiguous installed direct stream: "+name);result=&file;
    }
    if(!result)throw std::runtime_error("Direct stream is not converted: "+name);
    return result->path;
}
}
int Sound::free_audio_handle(bool sequenced)const{
    for(int candidate=0;candidate<(sequenced?8:4);++candidate){
        bool used=!sequenced && movie_channels_[candidate];for(const auto& stream:streams_)used|=stream.sequenced==sequenced && stream.handle==candidate && stream.audio && stream.audio->active();
        for(const auto& stream:fading_streams_)used|=stream.sequenced==sequenced && stream.handle==candidate && stream.audio && stream.audio->active();
        if(!sequenced)for(const auto& stream:direct_streams_)used|=stream.handle==candidate;
        if(!used)return candidate;
    }
    return -1;
}
int Sound::free_pcm_driver()const{
    for(int driver=0;driver<8;++driver){
        bool used=false;
        const auto scan=[&](const auto& list){for(const auto& s:list)used|=!s.sequenced&&s.driver==driver&&s.audio&&s.audio->active();};
        scan(streams_);scan(fading_streams_);scan(direct_streams_);
        if(!used)return driver;
    }
    return -1;
}
const Sound::Stream* Sound::application_stream(int handle)const{
    for(const auto& s:direct_streams_)if(s.handle==handle)return &s;
    for(const auto& s:streams_)if(!s.sequenced&&s.handle==handle&&s.audio)return &s;
    for(const auto& s:fading_streams_)if(!s.sequenced&&s.handle==handle&&s.audio)return &s;
    return nullptr;
}
std::unique_ptr<AudioPlayback> Sound::sequence(const std::vector<ResourceFile>& bank_files,const std::string& name){
    const auto& sequence_files=resources_.find_files(name+".sq");std::string path;
    for(const auto& file:sequence_files)if((file.conversion=="music-sequence-v2" || file.conversion=="music-pcm-v1") && std::filesystem::path(file.path).extension()==".tsv"){
        if(!path.empty())throw std::runtime_error("Ambiguous installed music sequence");path=file.path;
    }
    if(path.empty())throw std::runtime_error("Music sequence is not converted: "+name);
    const auto folder=std::filesystem::path(path).parent_path();bool matching=false;
    for(const auto& file:bank_files)if(file.conversion=="music-sequence-v2" || file.conversion=="music-pcm-v1"){
        const auto parent=std::filesystem::path(file.path).parent_path();matching|=parent==folder || (parent.filename()=="samples" && parent.parent_path()==folder);
    }
    if(!matching)throw std::runtime_error("Sequence does not belong to selected music bank");
    return std::make_unique<SequencePlayback>(assets_,path,silent_);
}
Bindings Sound::stream_bindings(){
    Bindings bindings;
    bindings.emplace(755,Binding{0,0,[](const Invocation&){return Reply{false,mono_output.load()?1.f:0.f};}});
    bindings.emplace(756,Binding{1,0,[](const Invocation& c){mono_output=number(c.arguments[0])==1;return Reply{false,0.f};}});
    const auto integer=[](float value){const float n=std::trunc(value);if(!std::isfinite(n)||double(n)<INT32_MIN||double(n)>INT32_MAX)throw std::runtime_error("Invalid direct stream argument");return int(n);};
    bindings.emplace(730,Binding{4,0,[this,integer](const Invocation& c){
        const int pointer=integer(c.arguments[0]),bytes=integer(c.arguments[1]),channels=integer(c.arguments[2]),rate=integer(c.arguments[3]);
        if(pointer<0||channels!=2||rate<=0||rate>192000)throw std::runtime_error("Invalid direct PCM stream format");
        const int handle=free_audio_handle(false);if(handle<0)return Reply{false,-1.f};
        const int driver=free_pcm_driver();if(driver<0)return Reply{false,-1.f};
        std::optional<std::uint64_t> frames;if(bytes>=0)frames=std::uint64_t(bytes/2048)*2048/16*28/unsigned(channels);
        auto audio=std::make_unique<PcmStream>(assets_,installed_path(resources_,read_script_string(c,float(pointer)*4.f)),unsigned(channels),unsigned(rate),frames,false,1.f,silent_);
        Stream stream;stream.audio=std::move(audio);stream.handle=handle;stream.driver=driver;stream.state=1;
        direct_streams_.push_back(std::move(stream));return Reply{false,float(handle)};
    }});
    for(unsigned opcode:{732u,733u})bindings.emplace(opcode,Binding{1,0,[this,integer,opcode](const Invocation& c){
        const int handle=integer(c.arguments[0]);if(handle<0||handle>=4)throw std::runtime_error("Invalid application stream handle");
        if(movie_channels_[handle])throw std::runtime_error("Movie application stream status binding is not connected");
        const auto* s=application_stream(handle);
        if(opcode==732)return Reply{false,s?float(s->state?s->state:s->audio->active()?3:4):0.f};
        return Reply{false,s&&s->state!=1?float(s->driver):-1.f};
    }});
    bindings.emplace(721,Binding{1,0,[this,integer](const Invocation& c){
        const int driver=integer(c.arguments[0]);if(driver<0)return Reply{false,-1.f};if(driver>=8)return Reply{false,0.f};
        const Stream* found=nullptr;
        const auto scan=[&](const auto& list){for(const auto& s:list)if(!s.sequenced&&s.driver==driver&&s.audio&&s.state!=1){if(!found||s.audio->active())found=&s;}};
        scan(streams_);scan(fading_streams_);scan(direct_streams_);
        return Reply{false,found?(found->audio->active()?5.f:13.f):0.f};
    }});
    bindings.emplace(729,Binding{0,0,[this](const Invocation&){
        // 139610 -> 1f0428 closes all eight sequence-engine records with
        // zero fade. PCM streams and script bank selections are independent.
        for(auto& stream:streams_)if(stream.sequenced)stream.audio.reset();
        std::erase_if(fading_streams_,[](const auto& stream){return stream.sequenced;});
        return Reply{};
    }});
    bindings.emplace(731,Binding{1,0,[this](const Invocation& c){
        const int handle=number(c.arguments[0]);
        if(handle>=4)throw std::runtime_error("Invalid engine stream handle");
        // 139788 -> CAppStream::Close: addresses engine handles, not the
        // four script slots used by 708. Sequence music is another service.
        for(int i=handle<0?0:handle;i<(handle<0?4:handle+1);++i){
            if(movie_channels_[i]){
                if(!close_movie_audio_)throw std::runtime_error("Movie audio owner is not connected");
                close_movie_audio_(i);movie_channels_[i]=false;
            }
            for(auto& stream:streams_)if(!stream.sequenced && stream.handle==i)stream.audio.reset();
            std::erase_if(fading_streams_,[i](const auto& stream){return !stream.sequenced && stream.handle==i;});
            std::erase_if(direct_streams_,[i](const auto& stream){return stream.handle==i;});
        }
        return Reply{false,0.f};
    }});
    for(unsigned opcode:{706u,707u})bindings.emplace(opcode,Binding{opcode==706?3u:2u,0,[this,opcode](const Invocation& c){
        const auto slot=number(c.arguments[0]);if(slot<0)return Reply{false,0.f};if(slot>=4)throw std::runtime_error("Invalid music sequence slot");
        const auto name=[&](unsigned argument){const auto address=number(c.arguments[argument]);if(address<0 || std::size_t(address)>=c.program.size())throw std::runtime_error("Invalid music resource name");return c.program.string(unsigned(address));};
        auto& stream=streams_[slot];const auto bank=opcode==706?name(1):stream.bank;
        if(bank.empty())return Reply{false,0.f};
        auto bank_files=opcode==706?resources_.find_files(bank+".hd"):stream.bank_files;
        auto audio=sequence(bank_files,name(opcode==706?2:1));stream={};
        const auto handle=free_audio_handle(true);stream.bank=bank;stream.bank_files=std::move(bank_files);stream.sequenced=true;
        if(handle>=0){stream.audio=std::move(audio);stream.handle=handle;}
        return Reply{false,0.f};
    }});
    bindings.emplace(751,Binding{6,0,[this](const Invocation& c){
        const auto slot=number(c.arguments[0]);if(slot<0)return Reply{false,0.f};
        if(slot>=4)throw std::runtime_error("Invalid music stream slot");
        const auto address=number(c.arguments[1]),bytes=number(c.arguments[2]),channels=number(c.arguments[3]),rate=number(c.arguments[4]),loop=number(c.arguments[5]);
        if(address<0 || std::size_t(address)>=c.program.size() || channels!=2 || rate<=0 || rate>192000)throw std::runtime_error("Invalid authored stream format");
        std::optional<std::uint64_t> frames;if(bytes>=0)frames=std::uint64_t(bytes/2048)*2048/16*28/unsigned(channels);
        auto audio=std::make_unique<PcmStream>(assets_,installed_path(resources_,c.program.string(unsigned(address))),unsigned(channels),unsigned(rate),frames,loop>=0,slot==0?0x2300/float(0x3fff):1.f,silent_);
        streams_[slot]={};
        const auto handle=free_audio_handle(false);
        if(handle>=0){streams_[slot].driver=free_pcm_driver();streams_[slot].audio=std::move(audio);streams_[slot].handle=handle;}
        return Reply{false,0.f};
    }});
    for(unsigned opcode:{708u,709u,710u,754u})bindings.emplace(opcode,Binding{opcode==754?2u:1u,0,[this,opcode](const Invocation& c){
        const auto slot=number(c.arguments[0]);if(slot>=4)throw std::runtime_error("Invalid music stream slot");
        const auto fade=opcode==754?number(c.arguments[1]):0;
        for(unsigned i=slot<0?0:unsigned(slot);i<(slot<0?4:unsigned(slot)+1);++i){
            auto& stream=streams_[i];if(!stream.audio){if(opcode==708 || opcode==754)stream={};continue;}
            if(opcode==709 || opcode==710){stream.paused=opcode==710;stream.audio->pause(stream.paused);}
            else{
                if(fade>0){stream.audio->fade(unsigned(fade));fading_streams_.push_back(std::move(stream));}
                else stream.audio.reset();stream={};
            }
        }
        return Reply{false,0.f};
    }});
    // Loading is synchronous in PcmStream. Readiness is independent of pause:
    // entry_094 deliberately issues 751, 710, 711, 709 in that order.
    bindings.emplace(711,Binding{1,0,[](const Invocation& c){const auto slot=number(c.arguments[0]);if(slot>=4)throw std::runtime_error("Invalid stream readiness slot");return Reply{false,0.f};}});
    bindings.emplace(712,Binding{1,0,[this](const Invocation& c){const auto slot=number(c.arguments[0]);if(slot>=4)throw std::runtime_error("Invalid stream handle slot");return Reply{false,float(slot<0?-1:streams_[slot].handle)};}});
    return bindings;
}
}
