#include "rendering/renderer.hpp"
#include "game/scene.hpp"
#include "rendering/lighting.hpp"
#include "rendering/model.hpp"
#include <cstring>

namespace lifeline {
namespace {
bool same_casters(std::span<const ShadowCaster> a,std::span<const ShadowCaster> b){
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){
        if(std::memcmp(&a[i].root,&b[i].root,sizeof(math::Vector4))||a[i].indices!=b[i].indices||a[i].vertices.size()!=b[i].vertices.size())return false;
        if(!a[i].vertices.empty()&&std::memcmp(a[i].vertices.data(),b[i].vertices.data(),a[i].vertices.size()*sizeof(math::Vector4)))return false;
    }
    return true;
}
void renderer_coordinates(ModelMesh& mesh){
    for(auto& vertex:mesh.vertices){vertex.position.y=-vertex.position.y;vertex.normal.y=-vertex.normal.y;}
    // Bounds and vertices must occupy the same space as the reflected camera.
    if(mesh.culling_corners)for(auto& point:*mesh.culling_corners)point.y=-point.y;
}
}
ActorScene::ActorScene(std::span<const unsigned char> actor)
    :skeleton_(decode_skeleton(actor)),skin_(decode_skinned_geometry(actor,skeleton_)){
    actor_=decode_skin_surface(actor,skin_);
}
ActorScene::ActorScene(std::span<const unsigned char> room,std::span<const unsigned char> actor)
    :room_(decode_static_model(room)),skeleton_(decode_skeleton(actor)),skin_(decode_skinned_geometry(actor,skeleton_)){
    actor_=decode_skin_surface(actor,skin_);
}
AssetImage ActorScene::render(const SceneCamera& source,std::span<const math::Matrix4> pose,bool visible,const script::Objects::Instance* room,const script::Objects* objects,const StaticModel* effects,bool transparent,SceneRenderer* renderer,bool high_resolution){
    AssetImage result{};render_into(result,source,pose,visible,room,objects,effects,transparent,renderer,high_resolution);return result;
}
void ActorScene::render_into(AssetImage& image,const SceneCamera& source,std::span<const math::Matrix4> pose,bool visible,const script::Objects::Instance* room,const script::Objects* objects,const StaticModel* effects,bool transparent,SceneRenderer* renderer,bool high_resolution){
    // Geometry source and destination node are independent script selections.
    // Rebuild both room and object instances when a node changes its source.
    auto previous_shadow=std::move(scene_.shadow);
    const bool previous_shadow_valid=shadow_valid_;shadow_valid_=false;
    const auto& base=room?room->model:room_;
    scene_.textures=base.textures;scene_.lights=base.lights;scene_.shadow=base.shadow;
    scene_.nodes=room?room->render_node_count():base.nodes;
    scene_.meshes=room?room->render_meshes():base.meshes;
    const auto& lights=objects && objects->lights().initialized()?objects->lights().records():scene_.lights;
    const auto groups=objects?objects->lights().groups():0xffffffff;
    const auto forward=math::normalize_xyz(source.target+source.eye*(-1));
    for(auto& mesh:scene_.meshes)if(mesh.scene_lighting)light_node(mesh,lights,groups,forward,source.fill_light);
    for(auto& mesh:scene_.meshes)renderer_coordinates(mesh);
    std::vector<unsigned char> nodes=room?room->visibility():std::vector<unsigned char>(scene_.nodes,1);
    if(room&&!room->visible)std::fill(nodes.begin(),nodes.end(),0);
    const unsigned actor_node=scene_.nodes++,actor_texture=unsigned(scene_.textures.size());
    nodes.push_back(visible && !pose.empty());
    const auto* replacement=objects?objects->actor_geometry():nullptr;
    StaticModel actor_surface;
    if(replacement){
        actor_surface.meshes=replacement->surface.meshes;
        if(!pose.empty())pose_skin_surface(actor_surface,replacement->skin,*objects->actor_skeleton(),pose);
    }else {
        if(!pose.empty())pose_skin_surface(actor_,skin_,skeleton_,pose);
        actor_surface.meshes=actor_.meshes;
    }
    std::vector<ShadowCaster> casters;
    const auto& quality=scene_graphics_quality();const bool shadows=quality.shadow_quality!=0;
    const auto& shadow_geometry=(replacement?replacement->skin:skin_).shadow;
    if(shadows&&source.shadow.mode&&visible&&!pose.empty()&&shadow_geometry&&(!objects||objects->actor_shadow_policy()!=0))
        casters.push_back({pose.front().columns[3],pose_shadow_geometry(*shadow_geometry,pose),shadow_geometry->indices});
    const auto position=pose.empty()?math::Vector4{}:pose.front().columns[3];
    light_actor(actor_surface,lights,objects?objects->lights().groups():0xffffffff,position,
        forward,objects?objects->actor_material_bias():math::Vector4{},source.fill_light);
    const auto& actor_textures=(replacement?replacement->surface:actor_).textures;
    scene_.textures.insert(scene_.textures.end(),actor_textures.begin(),actor_textures.end());
    for(auto mesh:actor_surface.meshes){
        mesh.node=actor_node;mesh.texture+=actor_texture;
        if(objects){
            mesh.refraction=objects->actor_refraction();
        }
        scene_.meshes.push_back(std::move(mesh));
    }
    if(quality.reflections && source.reflection_enabled && visible && !pose.empty() && (replacement?replacement->skin:skin_).planar_reflection && (!objects||objects->actor_refraction().strength<128))for(const auto& mesh:actor_surface.meshes){
        auto reflected=reflect_skin_mesh(mesh,source.reflection);reflected.node=actor_node;reflected.texture+=actor_texture;
        scene_.meshes.push_back(std::move(reflected));
    }
    auto camera=source;camera.eye.y=-camera.eye.y;camera.target.y=-camera.target.y;camera.up.y=-camera.up.y;camera.mirror_horizontal=true;
    if(objects)for(unsigned slot=0;slot<32;++slot){
        const auto* object=objects->instance(slot);if(!object || object==room || !object->visible)continue;
        if(shadows&&source.shadow.mode&&object->shadow_policy&&object->skin&&object->skin->shadow&&!object->skeletal_pose.empty()){
            const auto& geometry=*object->skin->shadow;auto vertices=pose_shadow_geometry(geometry,object->skeletal_pose);const auto world=object->world_matrix();
            for(auto& v:vertices)v=math::transform(world,v);
            casters.push_back({math::transform(world,object->skeletal_pose.front().columns[3]),std::move(vertices),geometry.indices});
        }
        const auto active=object->visibility();const unsigned node_base=scene_.nodes,texture_base=unsigned(scene_.textures.size());
        scene_.nodes+=unsigned(active.size());nodes.insert(nodes.end(),active.begin(),active.end());
        scene_.textures.insert(scene_.textures.end(),object->model.textures.begin(),object->model.textures.end());
        auto object_meshes=object->render_meshes();
        if(object->skeleton){
            StaticModel character;character.meshes=std::move(object_meshes);
            light_character(character,lights,groups,object->world_matrix().columns[3],forward,source.fill_light);
            object_meshes=std::move(character.meshes);
        }
        for(auto& mesh:object_meshes){
            if(!object->skeleton&&mesh.scene_lighting)light_node(mesh,lights,groups,forward,source.fill_light);
            mesh.node+=node_base;mesh.texture+=texture_base;
            renderer_coordinates(mesh);
            std::optional<ModelMesh> reflection;
            if(quality.reflections && source.reflection_enabled && object->skin && object->skin->planar_reflection && object->refraction.strength<128){
                reflection=reflect_skin_mesh(mesh,source.reflection);
            }
            scene_.meshes.push_back(std::move(mesh));
            if(reflection)scene_.meshes.push_back(std::move(*reflection));
        }
    }
    if(effects){
        const auto node_base=scene_.nodes,texture_base=unsigned(scene_.textures.size());
        scene_.nodes+=effects->nodes;nodes.insert(nodes.end(),effects->nodes,1);
        scene_.textures.insert(scene_.textures.end(),effects->textures.begin(),effects->textures.end());
        for(auto mesh:effects->meshes){
            mesh.node+=node_base;mesh.texture+=texture_base;
            renderer_coordinates(mesh);
            scene_.meshes.push_back(std::move(mesh));
        }
    }
    const auto shadow_size=shadow_resolution(quality.shadow_quality);
    // Compare the complete generated caster data. Poses, object visibility,
    // transforms, light settings and quality can all invalidate the cached map.
    if(previous_shadow_valid&&shadow_size_==shadow_size&&shadow_settings_.mode==source.shadow.mode&&shadow_settings_.opacity==source.shadow.opacity
        &&!std::memcmp(&shadow_settings_.direction,&source.shadow.direction,sizeof(math::Vector4))&&same_casters(shadow_casters_,casters))scene_.shadow=std::move(previous_shadow);
    else {
        make_shadow_map(previous_shadow,casters,source.shadow,shadow_size);scene_.shadow=std::move(previous_shadow);
        shadow_casters_=std::move(casters);shadow_settings_=source.shadow;shadow_size_=shadow_size;shadow_valid_=true;
    }
    shadow_valid_=true;
    if(renderer)renderer->render_into(image,{scene_,camera,nodes,transparent,high_resolution});else image=render_scene(scene_,camera,nodes,transparent);
    const float scale=frame_scale(image);
    if(!high_resolution&&(image.width!=640||image.height!=448))throw std::runtime_error("Scene renderer returned an unexpected frame size");
    // Drawing mode 2 also displays the generated shadow texture (1cf1c8).
    if(source.shadow.mode==2&&!scene_.shadow.mask.empty())for(unsigned y=0;y<256*scale;++y)for(unsigned x=0;x<256*scale;++x){
        const auto size=scene_.shadow.size,u=std::min(size-2,unsigned(x/scale*size/256)),v=std::min(size-2,unsigned(y/scale*size/256));const auto at=v*size+u;const float coverage=(scene_.shadow.mask[at]+scene_.shadow.mask[at+1]+scene_.shadow.mask[at+size]+scene_.shadow.mask[at+size+1])/(4.f*255.f);
        const float alpha=coverage*scene_.shadow.opacity*.5f;auto* pixel=image.rgba.data()+((y+unsigned(16*scale))*image.width+x+unsigned(16*scale))*4;
        for(unsigned k=0;k<3;++k)pixel[k]=unsigned char(float(pixel[k])*(1-alpha));
    }
}
}
