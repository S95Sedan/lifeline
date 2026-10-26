#pragma once
#include "core/math.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace lifeline {
struct StaticModel;
struct ModelMesh;

// Portable values recovered from SOLCOM's scene-light records.
struct SceneLight {
    unsigned type=0,group=0,shading=0,attenuation=1,rotation_order=0;
    math::Vector4 color{},position{0,0,1,1},rotation{},scale{1,1,1,0};
    math::Vector4 falloff{1,0,0,0};
    float range=1,cone=0,spread=40,exponent=1,bias=0,gain=1;
    float intensity(float distance)const;
};
std::vector<SceneLight> decode_scene_lights(std::span<const unsigned char> model);
class SceneLights {
    std::vector<SceneLight> records_;
    std::uint32_t groups_=0xffffffff;
    bool initialized_=false;
public:
    void load(std::span<const unsigned char> model);
    void clear(){records_.clear();initialized_=true;}
    void replace(std::span<const SceneLight> records){if(!records.empty()){records_.assign(records.begin(),records.end());initialized_=true;}}
    bool initialized()const{return initialized_;}
    void translate(math::Vector4 delta);
    void groups(std::uint32_t mask){groups_=mask;}
    std::uint32_t groups()const{return groups_;}
    const std::vector<SceneLight>& records()const{return records_;}
    std::vector<unsigned> enabled()const;
};

void light_actor(StaticModel&,std::span<const SceneLight>,std::uint32_t groups,
    math::Vector4 position,math::Vector4 camera_forward,math::Vector4 emission_bias={},math::Vector4 fill={});
void light_character(StaticModel&,std::span<const SceneLight>,std::uint32_t groups,
    math::Vector4 position,math::Vector4 camera_forward,math::Vector4 fill={});
void light_node(ModelMesh&,std::span<const SceneLight>,std::uint32_t groups,
    math::Vector4 camera_forward={},math::Vector4 fill={});
}
