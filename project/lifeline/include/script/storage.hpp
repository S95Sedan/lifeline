#pragma once
#include "script/runtime.hpp"
#include <deque>
#include "assets/resources.hpp"

namespace lifeline::script {
// Native storage requests progress at the original memory-card update point.
// File operations are supplied by the save/load owner, never interpreted as
// console device commands here.
class Storage {
    bool enabled_=false;
    std::deque<std::function<void()>> pending_;
    Resources* resources_=nullptr;
    std::filesystem::path root_,directory_;
    int type_=0,free_kib_=0,formatted_=0,result_=0,error_=0;
    std::uint64_t successful_writes_=0;
    bool complete_=false;
    std::filesystem::path path(const std::string&)const;
    void probe();
    void operation(unsigned,const std::string& = {},const std::string& = {},unsigned=0);
public:
    void configure(Resources& resources,const std::filesystem::path& root);
    void request(std::function<void()> operation){pending_.push_back(std::move(operation));}
    bool pending()const{return !pending_.empty();}
    bool enabled()const{return enabled_;}
    std::uint64_t successful_writes()const{return successful_writes_;}
    void advance(){
        if(!enabled_||pending_.empty())return;
        auto operation=std::move(pending_.front());pending_.pop_front();operation();
    }
    Bindings bindings();
};
}
