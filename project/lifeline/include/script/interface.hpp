#pragma once
#include "script/runtime.hpp"
#include "script/save_menu.hpp"
#include "assets/resources.hpp"
#include "assets/flash.hpp"
#include "rendering/model.hpp"
#include <array>

namespace lifeline::script {
// Native instances owned by one script process, matching its twelve CF slots.
class Interface {
public:
    struct Instance {
        FlashMovie movie;
        FlashPlayer player;
        math::Vector4 position{0,0,0,1};
        math::Vector4 rotation{0,0,0,0},scale{1,1,1,0};
        unsigned state=3,projection=0;
        int draw_order=10;
        std::uint64_t generation=0;
        std::optional<std::array<float,2>> pointer;
        SaveListWindow save_list;
        unsigned save_metadata=0;
        Instance(const ResourceFile& file,const std::string& name):movie(*file.bytes,name),player(movie){}
    };
    struct Draw {unsigned slot;FlashDraw geometry;};
    struct Fade {float alpha=0,step=0;std::array<float,4> rectangle{0,0,640,448};};
private:
    Resources& resources_;
    std::array<std::unique_ptr<Instance>,12> slots_;
    bool enabled_=false;
    bool scene_suspended_=false;
    Fade fade_;
    std::uint64_t generation_=0;
    std::function<void(unsigned,std::array<std::int32_t,3>)> message_;
    std::function<void()> reboot_;
    Instance* get(float);
    void pointer(Instance&,bool down);
public:
    explicit Interface(Resources& resources):resources_(resources){}
    Bindings bindings();
    void on_reboot(std::function<void()> sink){reboot_=std::move(sink);}
    void on_message(std::function<void(unsigned,std::array<std::int32_t,3>)> sink){message_=std::move(sink);}
    void advance();
    enum class Layer { all, background, foreground };
    std::vector<Draw> compose(Layer layer=Layer::all) const;
    void append_world(StaticModel&)const;
    void append_background(StaticModel&,const SceneCamera&)const;
    const Instance* instance(unsigned slot)const{return slots_.at(slot).get();}
    bool enabled()const{return enabled_;}
    bool scene_suspended()const{return scene_suspended_;}
    void resume_scene(){scene_suspended_=false;}
    void title_start_page(){
        for(auto& slot:slots_)if(slot){
            if(slot->movie.name=="tit_load.cf"){slot->player.seek(3);slot->pointer.reset();}
            else if(slot->movie.name=="tit_loadcur.cf")slot.reset();
        }
    }
    const Fade& fade()const{return fade_;}
};
}
