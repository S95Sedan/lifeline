#include "assets/resources.hpp"
#include <sstream>
#include <execution>

namespace lifeline {
namespace {
template<class Members>
auto find_archive_member(const Members& members,const std::string& requested){
    auto found=members.find(requested);
    // The original archive directory stores and compares a maximum of 16
    // member-name bytes. Scripts can retain the untruncated source filename.
    if(found==members.end() && requested.size()>16)found=members.find(requested.substr(0,16));
    return found;
}
}
unsigned Resources::address(const std::string& member){
    // Replace the original heap address with a stable, exactly representable
    // native byte handle. Lifetimes still follow the owning loaded archive.
    const auto* files=first_loaded_files(member);if(!files)return 0;
    if(files->size()!=1||files->front().conversion!="raw")throw std::runtime_error("Resource address requires raw byte data: "+member);
    const auto bytes=files->front().bytes;
    for(const auto& entry:addresses_)if(!entry.bytes.owner_before(bytes)&&!bytes.owner_before(entry.bytes))return entry.base;
    const auto size=(std::uint64_t(bytes->size())+15)&~15ull;
    if(size+next_address_>16777216)throw std::runtime_error("Resource byte handles exceed exact script integer range");
    const unsigned base=next_address_;next_address_+=unsigned(std::max(size,16ull));addresses_.push_back({base,bytes});return base;
}
std::span<const unsigned char> Resources::addressed(unsigned address)const{
    for(const auto& entry:addresses_)if(address>=entry.base){
        const auto bytes=entry.bytes.lock();if(bytes && address-entry.base<bytes->size())return std::span<const unsigned char>(*bytes).subspan(address-entry.base);
    }
    throw std::runtime_error("Expired or invalid resource byte handle");
}
std::string Resources::key(std::string name){
    if(name.starts_with("/archive/"))name.erase(0,9);
    if(name.empty() || name.front()=='/' || name.find_first_of("\\:\t\r\n")!=std::string::npos || name.find("..")!=std::string::npos)throw std::runtime_error("Invalid installed resource name");
    return name;
}
Resources::Resources(const AssetDirectory& assets):assets_(assets),cache_(assets){
    const auto bytes=assets.read("resources.tsv");std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string line;
    if(!std::getline(input,line) || line!="LIFELINE_RESOURCE_CATALOG\t1")throw std::runtime_error("Unsupported installed resource catalog");
    while(std::getline(input,line)){
        std::vector<std::string> fields;std::istringstream row(line);std::string field;while(std::getline(row,field,'\t'))fields.push_back(field);
        if(fields.size()==3 && fields[0]=="archive"){
            if(fields[2]!="0" && fields[2]!="1")throw std::runtime_error("Invalid resource completeness marker");
            Group group;group.complete=fields[2]=="1";
            if(!groups_.emplace(key(fields[1]),std::move(group)).second)throw std::runtime_error("Duplicate resource group");
        }else if(fields.size()==5 && fields[0]=="file"){
            auto found=groups_.find(key(fields[1]));if(found==groups_.end())throw std::runtime_error("Resource file precedes its group");
            if(fields[2].empty() || fields[3].empty())throw std::runtime_error("Invalid resource member");
            // Validate the installed path with the same boundary used by reads.
            assets_.resolve(fields[4]);found->second.members[fields[2]].push_back({fields[3],fields[4],{}});
        }else throw std::runtime_error("Invalid installed resource catalog row");
    }
}
const ResourceFile& Resources::installed_raw(const std::string& archive,const std::string& member) const {
    const auto group=groups_.find(key(archive));
    if(group==groups_.end())throw std::runtime_error("Resource is not installed: "+archive);
    const auto found=find_archive_member(group->second.members,member);
    if(found==group->second.members.end())throw std::runtime_error("Installed resource member not found: "+member);
    if(found->second.size()!=1 || found->second.front().conversion!="raw")throw std::runtime_error("Resource needs a typed conversion reader: "+member);
    return found->second.front();
}
Bytes Resources::read_raw(const std::string& archive,const std::string& member) const {const auto& file=installed_raw(archive,member);return file.bytes?*file.bytes:assets_.read(file.path);}
unsigned Resources::load(const std::string& archive){
    const auto name=key(archive);const auto it=groups_.find(name);
    if(it==groups_.end())throw std::runtime_error("Resource is not installed: "+name);
    auto& group=it->second;if(!group.complete)throw std::runtime_error("Resource conversion is incomplete: "+name);
    if(group.loaded)return 0;
    // Stage reads before committing so a missing file never leaves a partial load.
    auto members=group.members;
    struct Read {std::string path;std::shared_ptr<const Bytes> payload;std::exception_ptr error;};
    std::vector<Read> reads;std::map<std::string,std::size_t> paths;
    for(const auto& [member,files]:members)for(const auto& file:files)
        if(paths.emplace(file.path,reads.size()).second)reads.push_back({file.path});
    const auto read=[&](Read& entry){try{entry.payload=cache_.read(entry.path);}catch(...){entry.error=std::current_exception();}};
    // Keep publication on the calling thread, in the existing member order.
    // Exceptions must be caught inside a parallel STL callback.
    if(reads.size()>=8)std::for_each(std::execution::par,reads.begin(),reads.end(),read);
    else for(auto& entry:reads)read(entry);
    for(const auto& entry:reads)if(entry.error)std::rethrow_exception(entry.error);
    for(auto& [member,files]:members)for(auto& file:files){
        const auto& payload=reads[paths.at(file.path)].payload;
        // A fresh ownership lease on every load preserves weak byte-handle
        // expiration after unload, even while the cache retains the payload.
        auto lease=std::make_shared<std::shared_ptr<const Bytes>>(payload);
        file.bytes=std::shared_ptr<const Bytes>(std::move(lease),payload.get());
    }
    unsigned slot=0;
    for(;;++slot){bool occupied=false;for(const auto& [name,other]:groups_)if(other.loaded && other.lookup_slot==slot){occupied=true;break;}if(!occupied)break;}
    group.lookup_slot=slot;group.members=std::move(members);group.loaded=true;group.generation=next_request_;ready_=true;return next_request_++;
}
void Resources::unload(const std::string& archive){
    const auto it=groups_.find(key(archive));if(it==groups_.end())return;
    for(auto& [member,files]:it->second.members)for(auto& file:files)file.bytes.reset();it->second.loaded=false;
}
bool Resources::loaded(const std::string& archive) const {const auto it=groups_.find(key(archive));return it!=groups_.end() && it->second.loaded;}
void Resources::checkpoint(){
    std::map<std::string,unsigned> loaded;
    for(const auto& [name,group]:groups_)if(group.loaded)loaded.emplace(name,group.generation);
    for(const auto& [name,resource]:generated_)loaded.emplace("@"+name,resource.generation);
    checkpoints_.push_back(std::move(loaded));
}
void Resources::restore_checkpoint(){
    if(checkpoints_.empty())throw std::runtime_error("Resource checkpoint stack is empty");
    const auto& saved=checkpoints_.back();
    for(auto& [name,group]:groups_)if(group.loaded&&(!saved.contains(name)||saved.at(name)!=group.generation))unload(name);
    std::erase_if(generated_,[&](const auto& entry){const auto id="@"+entry.first;return !saved.contains(id)||saved.at(id)!=entry.second.generation;});
    checkpoints_.pop_back();
}
void Resources::store(const std::string& name,Bytes bytes){
    const auto id=key(name);auto found=generated_.find(id);
    if(found!=generated_.end())found->second.bytes=std::move(bytes);
    else generated_.emplace(id,Generated{std::move(bytes),next_request_++});
}
std::optional<Bytes> Resources::stored(const std::string& name)const{
    const auto found=generated_.find(key(name));if(found==generated_.end())return {};return found->second.bytes;
}
const ResourceFile& Resources::find_raw(const std::string& member) const {
    const ResourceFile* result=nullptr;
    for(const auto& [name,group]:groups_)if(group.loaded){
        const auto found=find_archive_member(group.members,member);if(found==group.members.end())continue;
        for(const auto& file:found->second){
            if(file.conversion!="raw")throw std::runtime_error("Resource needs a typed conversion reader: "+member);
            if(result && *result->bytes!=*file.bytes)throw std::runtime_error("Ambiguous loaded resource member: "+member);
            result=&file;
        }
    }
    if(!result)throw std::runtime_error("Resource member is not loaded: "+member);
    return *result;
}
const std::vector<ResourceFile>& Resources::find_files(const std::string& member) const {
    const auto* result=find_loaded_files(member);
    if(!result)throw std::runtime_error("Resource member is not loaded: "+member);
    return *result;
}
const std::vector<ResourceFile>* Resources::find_loaded_files(const std::string& member) const {
    const std::vector<ResourceFile>* result=nullptr;
    for(const auto& [name,group]:groups_)if(group.loaded){
        const auto found=find_archive_member(group.members,member);if(found==group.members.end())continue;
        if(result)throw std::runtime_error("Ambiguous loaded resource member: "+member);
        result=&found->second;
    }
    return result;
}
const std::vector<ResourceFile>* Resources::first_loaded_files(const std::string& member) const {
    const Group* first=nullptr;const std::vector<ResourceFile>* result=nullptr;
    for(const auto& [name,group]:groups_)if(group.loaded && (!first || group.lookup_slot<first->lookup_slot)){
        const auto found=find_archive_member(group.members,member);if(found==group.members.end())continue;
        first=&group;result=&found->second;
    }
    return result;
}
const std::vector<ResourceFile>& Resources::files(const std::string& archive,const std::string& member) const {
    const auto it=groups_.find(key(archive));if(it==groups_.end() || !it->second.loaded)throw std::runtime_error("Resource is not loaded: "+archive);
    const auto file=find_archive_member(it->second.members,member);if(file==it->second.members.end())throw std::runtime_error("Installed resource member not found: "+member);return file->second;
}
const std::vector<ResourceFile>& Resources::installed_files(const std::string& archive,const std::string& member) const {
    const auto it=groups_.find(key(archive));if(it==groups_.end())throw std::runtime_error("Resource is not installed: "+archive);
    const auto file=find_archive_member(it->second.members,member);if(file==it->second.members.end())throw std::runtime_error("Installed resource member not found: "+member);return file->second;
}
}
