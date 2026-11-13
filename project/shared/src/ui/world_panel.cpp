#include "ui/world_panel.hpp"

namespace lifeline {
WorldPanel::WorldPanel(StaticModel room,const FlashMovie& movie)
    :scene_(std::move(room)),player_(movie),node_(scene_.nodes),room_meshes_(scene_.meshes.size()) {
    if(!scene_.nodes || scene_.nodes>=1024)throw std::runtime_error("Invalid world panel scene");
    ++scene_.nodes;
    for(const auto& [id,texture]:movie.textures){textures_[id]=unsigned(scene_.textures.size());scene_.textures.push_back(texture);}
    white_=unsigned(scene_.textures.size());scene_.textures.push_back({"Panel solid fill",1,1,Bytes{255,255,255,255}});
}
void WorldPanel::update(const math::Matrix4& transform,bool visible) {
    scene_.meshes.resize(room_meshes_);
    if(!visible)return;
    for(const auto& draw:player_.compose()){
        ModelMesh mesh;mesh.node=node_;mesh.texture=draw.textured?textures_.at(draw.texture):white_;mesh.depth_write=false;
        mesh.vertices.reserve(draw.vertices.size());mesh.indices.reserve(draw.indices.size());
        for(const auto& v:draw.vertices){const auto p=math::transform(transform,{v.x,v.y,0,1});
            if(p.w!=1 || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))throw std::runtime_error("Invalid world panel transform");
            mesh.vertices.push_back({p,{0,0,0,0},v.color,v.u,v.v});}
        for(const auto index:draw.indices){if(index<0 || std::size_t(index)>=mesh.vertices.size())throw std::runtime_error("Invalid world panel index");mesh.indices.push_back(unsigned(index));}
        scene_.meshes.push_back(std::move(mesh));
    }
}
}
