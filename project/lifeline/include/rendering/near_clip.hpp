#pragma once
#include <cmath>
#include <cstddef>
#include <algorithm>

namespace lifeline::near_clip {
template<class Mesh> bool opaque_room_shell(const Mesh& mesh){
    return mesh.receives_shadow&&mesh.stream==0&&(mesh.flags>>24)==0&&
        !mesh.refraction.strength&&!mesh.captured_scene&&!mesh.scene_wave&&
        !mesh.effect_pass&&!mesh.effect_blend&&!mesh.background&&!mesh.world_surface&&mesh.depth_test&&mesh.depth_write;
}
inline float shell_guard(float near_plane,float focus_distance,bool opaque_room){
    // A native follow camera can sit just behind a room shell. Extend only
    // its reverse-face guard, bounded by two near distances and 1/4 focus.
    // Front faces, translucent surfaces and close-up cameras keep their clip.
    return opaque_room?std::max(near_plane,std::min(near_plane*2,focus_distance*.25f)):near_plane;
}
// Only call for an original near/eye-crossing triangle, after clipping to the
// positive near plane. Mirror semantics are already present in camera-space x.
// A boundary vertex can duplicate the fan anchor: inspect the first usable fan.
template<class Polygon>
bool reverse_wound_polygon(const Polygon& polygon,float half_width,float half_height,float focal){
    if(polygon.size()<3)return false;
    const auto project=[&](const auto& v){
        struct Point {float x,y;};
        return Point{half_width+v.x*focal/v.z,half_height-v.y*focal/v.z};
    };
    const auto a=project(polygon[0]);
    for(std::size_t i=1;i+1<polygon.size();++i){
        const auto b=project(polygon[i]),c=project(polygon[i+1]);
        const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        if(std::isfinite(area)&&std::abs(area)>=.00001f)return area<0;
    }
    return false;
}
}
