#pragma once
#include "ui/combat_targets.hpp"
#include "assets/asset_directory.hpp"
#include "rendering/model.hpp"

namespace lifeline {
class CombatPanel {
public:
    struct Panel {FlashMovie movie;FlashPlayer player;Panel(Bytes bytes,const std::string& name):movie(bytes,name),player(movie){}};
    struct Draw {unsigned panel;FlashDraw geometry;};
    using Notify=std::function<void(int,int,int)>;
private:
    const AssetDirectory& assets_;
    CombatTargets& targets_;
    std::function<int(int)> quantity_;
    Notify notify_;
    std::array<std::unique_ptr<Panel>,7> panels_;
    bool active_=false,shown_=true,closing_=false;
    int slide_=-1,toggle_=-1;
    bool hide_=false;
    float scale_=1.05f;
    std::uint64_t generation_=0;
    void ammunition(int);
    void items();
public:
    CombatPanel(const AssetDirectory& a,CombatTargets& t,std::function<int(int)> quantity,Notify notify)
        :assets_(a),targets_(t),quantity_(std::move(quantity)),notify_(std::move(notify)){}
    int message(int message,int first=0,int second=0,bool expanded=false);
    bool advance();
    std::vector<Draw> compose(const SceneCamera&);
    const auto& panels()const{return panels_;}
    auto generation()const{return generation_;}
    bool visible()const{return bool(panels_[0]);}
    void release_flash(){for(auto& panel:panels_)panel.reset();++generation_;}
    bool active()const{return active_;}
    bool targets_visible()const{return shown_;}
    float target_scale()const{return scale_;}
};
}
