#pragma once
#include "assets/asset_directory.hpp"
#include "assets/resource_cache.hpp"
#include <map>
#include <memory>
#include <optional>
#include <span>

namespace lifeline {
struct ResourceFile {std::string conversion,path;std::shared_ptr<const Bytes> bytes;};
class Resources {
    struct Group {bool complete=false;std::map<std::string,std::vector<ResourceFile>> members;bool loaded=false;unsigned lookup_slot=0,generation=0;};
    AssetDirectory assets_;
    ResourceCache cache_;
    std::map<std::string,Group> groups_;
    unsigned next_request_=1;
    bool ready_=false;
    std::vector<std::map<std::string,unsigned>> checkpoints_;
    struct Generated {Bytes bytes;unsigned generation=0;};
    std::map<std::string,Generated> generated_;
    struct Addressed {unsigned base;std::weak_ptr<const Bytes> bytes;};
    std::vector<Addressed> addresses_;
    unsigned next_address_=65536;
    static std::string key(std::string);
public:
    explicit Resources(const AssetDirectory&);
    Bytes read_raw(const std::string& archive,const std::string& member) const;
    const ResourceFile& installed_raw(const std::string& archive,const std::string& member) const;
    unsigned load(const std::string& archive);
    void unload(const std::string& archive);
    void checkpoint();
    void restore_checkpoint();
    void store(const std::string& name,Bytes bytes);
    std::optional<Bytes> stored(const std::string& name)const;
    bool ready() const {return ready_;}
    bool loaded(const std::string& archive) const;
    unsigned address(const std::string& member);
    std::span<const unsigned char> addressed(unsigned address)const;
    const std::vector<ResourceFile>& files(const std::string& archive,const std::string& member) const;
    const std::vector<ResourceFile>& installed_files(const std::string& archive,const std::string& member) const;
    const ResourceFile& find_raw(const std::string& member) const;
    const std::vector<ResourceFile>& find_files(const std::string& member) const;
    const std::vector<ResourceFile>* find_loaded_files(const std::string& member) const;
    const std::vector<ResourceFile>* first_loaded_files(const std::string& member) const;
};
}
