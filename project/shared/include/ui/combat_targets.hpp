#pragma once
#include "assets/flash.hpp"
#include <array>
#include <functional>

namespace lifeline {
// Target-label portion of service 809A. CF playback remains owned by the HUD.
class CombatTargets {
public:
    using Labels=std::array<std::optional<std::string>,9>;
    using Source=std::function<Labels()>;
    struct Marker {std::array<float,3> position{};int enemy=-1;};
    struct Group {Source source;std::array<int,8> states{};Marker marker{};int health=0;};
private:
    std::array<Group,3> groups_;
    std::array<std::string,9> displayed_{};
    int enemy_=-1,part_=-1;
    std::array<Marker,3> numbers_{};
    Marker selected_{};
    float health_=1,displayed_health_=1;
public:
    void reset();
    void install(unsigned enemy,Source);
    void install(unsigned enemy,Labels);
    void states(unsigned enemy,std::array<int,8>);
    void select(int enemy,int part);
    void refresh(unsigned enemy);
    // Message 32772: position, reserved, HP, reserved, eight part states.
    void update(int enemy,const std::array<float,14>&);
    float draw_health();
    void apply_health(FlashPlayer&)const;
    const auto& markers()const{return numbers_;}
    const auto& selected_marker()const{return selected_;}
    float health()const{return health_;}
    void apply(FlashPlayer&,FlashTextAlignment alignment=FlashTextAlignment::left)const;
    std::array<float,2> selection_position()const;
    const auto& displayed()const{return displayed_;}
    const auto& groups()const{return groups_;}
    int enemy()const{return enemy_;}
    int part()const{return part_;}
};
}
