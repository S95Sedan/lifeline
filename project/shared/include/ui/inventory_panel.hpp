#pragma once
#include "script/inventory.hpp"
#include "assets/flash.hpp"

namespace lifeline {
// Item (8098) and keyword (809f) displays share the authored scrolling list.
class InventoryPanel {
public:
    struct Panel {FlashMovie movie;FlashPlayer player;Panel(Bytes bytes,const std::string& name):movie(bytes,name),player(movie){}};
    struct Draw {unsigned panel;FlashDraw geometry;};
private:
    Resources& resources_;
    script::Inventory& inventory_;
    script::FlagBank flags_;
    bool keywords_,active_=false,closing_=false;
    int transition_=-1,category_=0,row_=0,scroll_=-1;
    float scene_scale_=1;
    std::array<std::unique_ptr<Panel>,3> panels_;
    std::vector<int> entries_;
    std::uint64_t generation_=0;
    std::function<void(int,int,int)> notify_;
    std::function<void(unsigned)> sound_;
    void initialize();
    void populate();
    void input(int);
public:
    InventoryPanel(Resources& resources,script::Inventory& inventory,bool keywords,std::function<void(int,int,int)> notify,std::function<void(unsigned)> sound):resources_(resources),inventory_(inventory),keywords_(keywords),notify_(std::move(notify)),sound_(std::move(sound)){}
    void flags(script::FlagBank flags){flags_=flags;}
    int message(int,int=0,int=0);
    bool advance();
    bool active()const{return active_;}
    int selected()const;
    int category()const{return category_;}
    float scene_scale()const{return scene_scale_;}
    unsigned mode()const{return keywords_?4:3;}
    const auto& panels()const{return panels_;}
    auto generation()const{return generation_;}
    void release_flash(){panels_={};++generation_;}
    std::vector<Draw> compose()const;
    script::Bindings bindings();
};
}
