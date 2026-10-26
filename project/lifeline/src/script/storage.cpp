#include "script/storage.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace lifeline::script {
namespace fs=std::filesystem;
namespace {
std::string native_save_name(const std::string& name){
    constexpr std::string_view legacy="BASLUS-20848";
    if(name==legacy)return "Savegame";
    if(name.starts_with(std::string(legacy)+"-"))return "Savegame"+name.substr(legacy.size());
    return name;
}
std::string name(const Invocation& c,unsigned argument){
    const auto value=c.arguments[argument];if(!std::isfinite(value)||value<0||double(value)>=c.program.size())throw std::runtime_error("Invalid storage resource pointer");
    return c.program.string(unsigned(value));
}
void replace_file(const fs::path& temporary,const fs::path& target){
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw fs::filesystem_error("Replace save",temporary,target,std::error_code(GetLastError(),std::system_category()));
#else
    fs::rename(temporary,target);
#endif
}
}
void Storage::configure(Resources& resources,const fs::path& root){
    resources_=&resources;root_=fs::absolute(root).lexically_normal();directory_.clear();
}
fs::path Storage::path(const std::string& name)const{
    if(!resources_||root_.empty())throw std::runtime_error("Native save storage is not configured");
    if(name.find_first_of("\\:")!=std::string::npos)throw std::runtime_error("Invalid save path");
    auto relative=fs::path(name);if(relative.has_root_directory())relative=relative.relative_path();else relative=directory_/relative;
    relative=relative.lexically_normal();for(const auto& part:relative)if(part=="..")throw std::runtime_error("Save path escapes storage directory");
    // Preserve the original script's logical card paths, but expose ordinary
    // PC directory/filenames on disk. Save payloads and slot numbering do not change.
    fs::path mapped;bool savedata=false;
    for(const auto& part:relative){
        if(mapped.empty()&&(part=="BASLUS-20848"||part=="savedata")){mapped/="savedata";savedata=true;}
        else mapped/=savedata?native_save_name(part.string()):part.string();
    }
    relative=std::move(mapped);
    auto current=root_;if(fs::is_symlink(fs::symlink_status(current)))throw std::runtime_error("Save root cannot be a symbolic link");
    for(const auto& part:relative){current/=part;if(fs::is_symlink(fs::symlink_status(current)))throw std::runtime_error("Save path cannot traverse a symbolic link");}
    return current;
}
void Storage::probe(){
    const auto root=path("/");fs::create_directories(root);const auto space=fs::space(root);
    type_=2;formatted_=1;free_kib_=int(std::min<std::uintmax_t>(space.available/1024,INT32_MAX));result_=0;
}
void Storage::operation(unsigned opcode,const std::string& first,const std::string& second,unsigned requested){
    complete_=false;result_=0;
    try{
        switch(opcode){
        case 841:case 853:probe();break; // PC storage has no card-format operation.
        case 854:formatted_=0;break; // Detach the logical card; never erase the PC save directory.
        case 846:{const auto next=path(first);if(!fs::is_directory(next)){result_=-4;break;}directory_=next.lexically_relative(root_);break;}
        case 848:{if(!fs::create_directory(path(first)))result_=-4;break;}
        case 855:{if(!fs::remove(path(first)))result_=-4;break;}
        case 849:{
            auto bytes=resources_->stored(first);if(!bytes){const auto* files=resources_->first_loaded_files(first);if(files&&files->size()==1&&files->front().conversion=="raw")bytes=*files->front().bytes;}
            if(!bytes){result_=-4;error_=4;break;}
            const auto target=path(second);auto temporary=target;temporary+=".pending";
            if(fs::exists(temporary))throw std::runtime_error("A pending save already exists");
            try{
                std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output){result_=-4;error_=4;break;}
                output.write(reinterpret_cast<const char*>(bytes->data()),std::streamsize(bytes->size()));output.flush();if(!output)throw std::runtime_error("Save write failed");output.close();if(!output)throw std::runtime_error("Save close failed");
                replace_file(temporary,target);
                ++successful_writes_;
            }catch(...){std::error_code ignored;fs::remove(temporary,ignored);throw;}
            break;
        }
        case 851:{
            std::ifstream input(path(second),std::ios::binary|std::ios::ate);if(!input){result_=-4;error_=5;break;}
            const auto size=input.tellg();if(size<0||size>16*1024*1024)throw std::runtime_error("Invalid save file size");
            Bytes bytes(static_cast<std::size_t>(size));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));if(!input)throw std::runtime_error("Save read failed");
            const bool context=bytes.size()>=4&&bytes[0]=='L'&&bytes[1]=='L'&&bytes[2]=='S'&&bytes[3]=='C';
            // Native contexts carry their own lengths; PS2's fixed allocation
            // request cannot truncate their variable-length UTF-8 records.
            if(!context&&bytes.size()<requested){result_=-1;error_=14;break;}
            if(!context)bytes.resize(requested);resources_->store(first,std::move(bytes));break;
        }
        default:throw std::logic_error("Invalid native storage operation");
        }
    }catch(const fs::filesystem_error&){result_=-1;error_=opcode==851?5:opcode==849?4:11;}
    catch(const std::runtime_error&){result_=-1;error_=opcode==851?14:opcode==849?14:11;}
    complete_=true;
}
Bindings Storage::bindings(){
    Bindings result{{840,Binding{0,1,[this](const Invocation& c){enabled_=c.operands[0]!=0;return Reply{};}}}};
    for(unsigned opcode:{842u,843u,844u,845u,852u})result.emplace(opcode,Binding{0,0,[this,opcode](const Invocation&){
        return Reply{false,float(opcode==842?type_:opcode==843?formatted_:opcode==844?free_kib_:opcode==845?int(complete_):error_)};
    }});
    result.emplace(850,Binding{0,0,[this](const Invocation&){if(pending()||!complete_)return Reply{true,{}};return Reply{false,float(result_)};}});
    for(unsigned opcode:{841u,846u,848u,849u,851u,853u,854u,855u})result.emplace(opcode,Binding{opcode==851?3u:opcode==849?2u:(opcode==846||opcode==848||opcode==855)?1u:0u,0,[this,opcode](const Invocation& c){
        if(!resources_)throw std::runtime_error("Native save storage is not configured");
        if(pending()){
            // 849/851 still return their enqueue result without waiting;
            // original 1d6740 rejects a second outstanding request.
            if(opcode==849)return Reply{false,1.f};if(opcode==851)return Reply{false,0.f};return Reply{true,{}};
        }
        const auto first=c.arguments.empty()?std::string{}:name(c,0);
        const auto second=opcode==849||opcode==851?name(c,1):std::string{};
        unsigned requested=0;
        if(opcode==851){const auto size=c.arguments[2];if(!std::isfinite(size)||size<0||size>16*1024*1024)throw std::runtime_error("Invalid save read size");requested=unsigned(size);}
        if(opcode==849&&!resources_->stored(first)&&!resources_->first_loaded_files(first)){
            result_=-4;error_=4;complete_=true;return Reply{false,0.f};
        }
        complete_=false;request([this,opcode,first,second,requested]{operation(opcode,first,second,requested);});
        if(opcode==853||opcode==854)return Reply{};return Reply{false,opcode==849?1.f:0.f};
    }});
    return result;
}
}
