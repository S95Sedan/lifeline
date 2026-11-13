#include "assets/resource_cache.hpp"
#include <map>
#include <list>
#include <mutex>

namespace lifeline {
struct ResourceCache::Impl {
    struct Entry {
        std::filesystem::path file;
        std::filesystem::file_time_type modified;
        std::shared_ptr<const Bytes> bytes;
        std::list<std::string>::iterator recent;
    };
    AssetDirectory assets;
    std::map<std::string,Entry> entries;
    std::list<std::string> recent;
    std::mutex mutex;
    std::size_t limit,resident=0;
    Impl(const AssetDirectory& a,std::size_t l):assets(a),limit(l){}
};
ResourceCache::ResourceCache(const AssetDirectory& assets,std::size_t limit):impl_(std::make_shared<Impl>(assets,limit)){}
std::shared_ptr<const Bytes> ResourceCache::read(const std::string& path){
    auto& cache=*impl_;
    // Resolve the current logical path even on hits. Retargeting or removing
    // a package link must not silently keep serving its former destination.
    const auto file=cache.assets.resolve(path);
    const auto modified=std::filesystem::last_write_time(file);
    const auto size=std::filesystem::file_size(file);
    {
        std::scoped_lock lock(cache.mutex);
        const auto found=cache.entries.find(path);
        if(found!=cache.entries.end()){
            auto& entry=found->second;
            if(file==entry.file&&modified==entry.modified&&size==entry.bytes->size()){
                cache.recent.splice(cache.recent.begin(),cache.recent,entry.recent);return entry.bytes;
            }
            cache.resident-=entry.bytes->size();cache.recent.erase(entry.recent);cache.entries.erase(found);
        }
    }
    // File I/O is outside the cache lock so independent archive members can
    // load concurrently. A failed read never publishes a partial entry.
    auto bytes=std::make_shared<const Bytes>(cache.assets.read(path));
    if(bytes->size()>cache.limit||std::filesystem::last_write_time(file)!=modified)return bytes;
    std::scoped_lock lock(cache.mutex);
    const auto old=cache.entries.find(path);
    if(old!=cache.entries.end()){cache.resident-=old->second.bytes->size();cache.recent.erase(old->second.recent);cache.entries.erase(old);}
    while(cache.resident+bytes->size()>cache.limit){
        const auto oldest=cache.entries.find(cache.recent.back());
        cache.resident-=oldest->second.bytes->size();cache.entries.erase(oldest);cache.recent.pop_back();
    }
    cache.recent.push_front(path);
    try{cache.entries.emplace(path,Impl::Entry{file,modified,bytes,cache.recent.begin()});}catch(...){cache.recent.pop_front();throw;}
    cache.resident+=bytes->size();return bytes;
}
}
