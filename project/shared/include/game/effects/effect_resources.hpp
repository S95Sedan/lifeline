#pragma once
#include "assets/resources.hpp"
#include "script/runtime.hpp"

namespace lifeline::script {
// The effect classes own static texture handles independently of live particles.
// Installed bitmap ownership survives release of the source resource group.
class EffectResources {
public:
    struct Texture {
        std::string member;
        unsigned width=0,height=0,pixel_format=0,usage=0;
        bool palette=false;
        std::shared_ptr<const Bytes> bitmap;
    };
private:
    Resources& resources_;
    std::map<unsigned,std::shared_ptr<const Texture>> owners_;
    void initialize(unsigned recipe);
    void release(unsigned recipe);
public:
    explicit EffectResources(Resources& resources):resources_(resources){}
    void load(int type);
    void unload(int type);
    void clear(){owners_.clear();}
    std::size_t owner_count()const{return owners_.size();}
    std::vector<std::shared_ptr<const Texture>> textures(int type)const;
    Bindings bindings();
};
}
