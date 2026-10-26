#pragma once
#include "assets/flash.hpp"
#include "rendering/model.hpp"

namespace lifeline {
// Owns a scene and a CF player. UI geometry is rebuilt independently of room
// geometry; textures are installed once. The movie must outlive this object.
class WorldPanel {
    StaticModel scene_;
    FlashPlayer player_;
    std::map<unsigned,unsigned> textures_;
    unsigned white_,node_;
    std::size_t room_meshes_;
public:
    WorldPanel(StaticModel room,const FlashMovie& movie);
    FlashPlayer& player(){return player_;}
    const FlashPlayer& player() const{return player_;}
    const StaticModel& scene() const{return scene_;}
    void update(const math::Matrix4& transform,bool visible=true);
};
}
