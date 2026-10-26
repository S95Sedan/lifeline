#include "ui/combat_targets.hpp"
#include <cmath>
#include <limits>

namespace lifeline {
void CombatTargets::reset(){groups_={};displayed_={};enemy_=part_=-1;numbers_={};selected_={};health_=displayed_health_=1;}
void CombatTargets::install(unsigned enemy,Source source){
    if(enemy>=groups_.size())throw std::runtime_error("Combat target group outside native 0..2 range");
    groups_[enemy].source=std::move(source);refresh(enemy);
}
void CombatTargets::install(unsigned enemy,Labels labels){install(enemy,[labels=std::move(labels)]{return labels;});}
void CombatTargets::states(unsigned enemy,std::array<int,8> states){groups_.at(enemy).states=states;refresh(enemy);}
void CombatTargets::refresh(unsigned enemy){
    auto& group=groups_.at(enemy);const auto labels=group.source?group.source():Labels{};
    // 14af18 replaces the eight body-part fields, but not text_E0 or selection.
    for(unsigned i=0;i<8;++i){
        if(!labels[i])group.states[i]=0;
        displayed_[i].clear();
        if(group.states[i]==1)displayed_[i]="@ph32@pv16"+*labels[i];
        else if(group.states[i]==2)displayed_[i]="@ph32@pv16@c50505030"+*labels[i]+"@pv00\n@ph99@pv16@c99000099X";
    }
}
void CombatTargets::select(int enemy,int part){
    if(enemy>=int(groups_.size()))throw std::runtime_error("Combat target selection outside native group range");
    // 14b740 assigns unformatted text when switching the selected enemy.
    const auto labels=enemy<0 || !groups_[enemy].source?Labels{}:groups_[enemy].source();
    for(unsigned i=0;i<9;++i)displayed_[i]=labels[i].value_or("");
    enemy_=enemy;part_=enemy<0?-1:part;
    // The original presentation reads the selected enemy's current service
    // record.  Keep that relationship when selection changes between update
    // messages: otherwise the label/selector can name the new enemy while the
    // marker and HP bar continue showing the previous one.
    if(enemy_>=0){
        const auto& group=groups_[unsigned(enemy_)];
        selected_=group.marker;
        health_=float(group.health)/1000.f;
    }else selected_={};
}
void CombatTargets::update(int enemy,const std::array<float,14>& values){
    if(enemy<0)return;
    if(enemy>=int(groups_.size()))throw std::runtime_error("Combat update outside native group range");
    const auto integer=[](float value){
        if(!std::isfinite(value)||double(value)<std::numeric_limits<int>::min()||double(value)>std::numeric_limits<int>::max())
            throw std::runtime_error("Invalid combat update integer");
        return int(value);
    };
    for(unsigned i=0;i<3;++i)if(!std::isfinite(values[i]))throw std::runtime_error("Invalid combat marker position");
    const int hp=integer(values[4]*1000.f);std::array<int,8> states;
    for(unsigned i=0;i<8;++i)states[i]=integer(values[6+i]);
    const Marker marker{{values[0],values[1],values[2]},enemy};
    auto& group=groups_[unsigned(enemy)];group.marker=marker;group.health=hp;
    // Retain every group's latest marker even while it is selected. Switching
    // away must immediately expose the old enemy's numbered marker too.
    numbers_[unsigned(enemy)]=marker;
    if(enemy==enemy_){
        // 14b2fc formats the PREVIOUS states before 14b518 copies the new
        // payload. Non-selected updates never replace the displayed labels.
        refresh(unsigned(enemy));selected_=marker;health_=values[4];
    }
    group.states=states;
}
float CombatTargets::draw_health(){
    // Native text_el callback 14b588 advances only when the bar is drawn.
    displayed_health_-=(displayed_health_-health_)*.125f;return displayed_health_;
}
void CombatTargets::apply_health(FlashPlayer& player)const{
    player.set_text("text_EN",selected_.enemy<0?"":std::to_string(selected_.enemy+1),9,16,{.02734375f,.796875f,.796875f,1});
    // The original custom callback replaces the text_el glyph rendering.
    player.set_text("text_el","",2,4);
}
void CombatTargets::apply(FlashPlayer& player,FlashTextAlignment alignment)const{
    for(unsigned i=0;i<8;++i)player.set_text("text_b"+std::to_string(i),displayed_[i],32,16,{1,1,1,1},alignment);
    player.set_text("text_E0",displayed_[8],32,16,{1,1,1,1},alignment);
}
std::array<float,2> CombatTargets::selection_position()const{
    constexpr std::array<std::array<float,2>,8> positions{{{-302,171.5f},{-302,195.5f},{-151,171.5f},{-151,195.5f},{8,171.5f},{8,195.5f},{158,171.5f},{158,195.5f}}};
    return part_>=0 && part_<8?positions[unsigned(part_)]:std::array<float,2>{-500,0};
}
}
