#include "rendering/raster_scene.hpp"
#include "rendering/near_clip.hpp"
#include "rendering/alpha_layers.hpp"
#include "rendering/renderer.hpp"
#include <bit>
#include <cstring>
#include <limits>
#include <map>

namespace lifeline {
namespace {
struct Vertex {float x,y,z,u,v;math::Vector4 color,world;};
Vertex interpolate(const Vertex& a,const Vertex& b,float t){
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,a.color*(1-t)+b.color*t,a.world*(1-t)+b.world*t};
}
float edge(const math::Vector4& a,const math::Vector4& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
void bin(RasterBatch& batch,unsigned width,unsigned height,unsigned columns,unsigned rows){
    // Build the packed tile table in two passes. The old representation made
    // one growable vector per 16x16 tile (4,480 vectors at the 2x hardware
    // resolution) for every capture batch and then copied all indices again.
    // Counts and prefix cursors retain the exact triangle order without that
    // allocation/copy churn.
    const unsigned tile_count=columns*rows;
    std::vector<unsigned> counts(tile_count),first(tile_count+1);
    struct TileBounds {unsigned x0,x1,y0,y1;};std::vector<TileBounds> bounds;bounds.reserve(batch.triangles.size());
    for(unsigned i=0;i<batch.triangles.size();++i){
        const auto& t=batch.triangles[i];const auto a=t.a.screen,b=t.b.screen,c=t.c.screen;
        const int x0=std::clamp(int(std::floor(std::min({a.x,b.x,c.x}))),0,int(width)-1)/16,x1=std::clamp(int(std::ceil(std::max({a.x,b.x,c.x}))),0,int(width)-1)/16;
        const int y0=std::clamp(int(std::floor(std::min({a.y,b.y,c.y}))),0,int(height)-1)/16,y1=std::clamp(int(std::ceil(std::max({a.y,b.y,c.y}))),0,int(height)-1)/16;
        bounds.push_back({unsigned(x0),unsigned(x1),unsigned(y0),unsigned(y1)});
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)++counts[unsigned(y)*columns+unsigned(x)];
    }
    for(unsigned i=0;i<tile_count;++i)first[i+1]=first[i]+counts[i];
    const unsigned header=tile_count*2;
    batch.tiles.resize(header+first.back());
    std::vector<unsigned> cursor(first.begin(),first.end()-1);
    for(unsigned i=0;i<tile_count;++i){batch.tiles[i*2]=header+first[i];batch.tiles[i*2+1]=counts[i];}
    for(unsigned i=0;i<batch.triangles.size();++i){
        const auto& b=bounds[i];for(unsigned y=b.y0;y<=b.y1;++y)for(unsigned x=b.x0;x<=b.x1;++x){const auto tile=y*columns+x;batch.tiles[header+cursor[tile]++]=i;}
    }
}
}
void prepare_raster_scene(RasterScene& result,const SceneSubmission& submission,float scale,unsigned width,unsigned height,const RasterTexelDestination& destination){
    const auto& model=submission.model;const auto& camera=submission.camera;const auto visibility=submission.visibility;
    if(std::isnan(camera.far_plane)||camera.far_plane<=camera.near_plane||model.nodes>1024||(!visibility.empty()&&visibility.size()!=model.nodes)||!std::isfinite(camera.focal_pixels)||camera.focal_pixels<=0||!std::isfinite(camera.near_plane)||camera.near_plane<=0)throw std::runtime_error("Invalid native scene camera or visibility");
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1),camera_right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(camera_right,forward),right=camera_right*(camera.mirror_horizontal?-1.f:1.f);
    const auto focus_delta=camera.target+camera.eye*-1;const float focus_distance=std::sqrt(math::dot_xyz(focus_delta,focus_delta));
    if(!std::isfinite(math::dot_xyz(forward,forward))||!std::isfinite(math::dot_xyz(right,right)))throw std::runtime_error("Nonfinite native camera");
    if(!std::isfinite(scale)||scale<=0||scale>12)throw std::runtime_error("Invalid native raster scale");
    if(bool(width)!=bool(height)||width>7680||height>7680)throw std::runtime_error("Invalid native raster dimensions");
    for(auto& batch:result.batches){batch.capture=false;batch.triangles.clear();}
    result.width=width?width:unsigned(std::lround(640*scale));result.height=height?height:unsigned(std::lround(448*scale));result.tile_columns=(result.width+15)/16;result.tile_rows=(result.height+15)/16;result.clear=submission.transparent?0:0xff000000;
    // Keep the authored focal length in both axes. A wider target extends the
    // horizontal frustum; it does not stretch or crop the original camera view.
    scale=std::min(float(result.width)/640,float(result.height)/448);
    const float focal=camera.focal_pixels*scale,half_width=result.width*.5f,half_height=result.height*.5f;
    if(!submission.transparent&&camera.clear_color.w>0)for(unsigned k=0;k<3;++k){const float c=k==0?camera.clear_color.x:k==1?camera.clear_color.y:camera.clear_color.z;result.clear|=unsigned(std::clamp(c,0.f,255.f))<<(k*8);}
    SceneTextures textures(model,&result.texture_cache);std::size_t texel_count=0;
    for(unsigned i=0;i<model.textures.size();++i){const auto& texture=textures.at(i);
        if(!texture.width||!texture.height||texture.rgba.size()!=std::size_t(texture.width)*texture.height*4)throw std::runtime_error("Invalid native mesh texture");
        texel_count+=texture.rgba.size()/4;
    }
    const bool active_shadow=scene_graphics_quality().shadow_quality&&!model.shadow.mask.empty();
    if(active_shadow){
        if(model.shadow.size<128||model.shadow.size>2048||model.shadow.mask.size()!=std::size_t(model.shadow.size)*model.shadow.size)throw std::runtime_error("Invalid scene shadow mask");
        if(!model.shadow.depth.empty()&&model.shadow.depth.size()!=model.shadow.mask.size())throw std::runtime_error("Invalid scene shadow depth");
        const auto count=model.shadow.mask.size();texel_count+=(count+3)/4+count;
    }
    std::size_t cursor=0;
    std::vector<unsigned> offsets;offsets.reserve(model.textures.size());
    for(unsigned i=0;i<model.textures.size();++i){const auto& texture=textures.at(i);
        offsets.push_back(unsigned(cursor));cursor+=texture.rgba.size()/4;
    }
    result.shadow_offset=unsigned(cursor);
    struct Draw {const ModelMesh* mesh;std::size_t triangle;float distance;int pass;bool refract;int alpha_phase=0;};std::array<std::vector<Draw>,6> draws;
    std::size_t triangle_count=0;for(const auto& mesh:model.meshes)triangle_count+=mesh.indices.size()/3;
    struct Layer {double depth=0;std::size_t corners=0;};std::map<std::pair<unsigned,unsigned>,Layer> layers;
    draws[1].reserve(triangle_count);
    const auto append=[&](Draw draw){draws[draw.pass+1].push_back(draw);};
    for(const auto& mesh:model.meshes){
        if(mesh.node>=model.nodes)throw std::runtime_error("Invalid native mesh node");
        if(!visibility.empty()&&!visibility[mesh.node])continue;
        if(mesh.reflection_pass&&!scene_graphics_quality().reflections)continue;
        if(mesh.culling_corners){unsigned common=63;for(auto p:*mesh.culling_corners){const auto d=p+camera.eye*-1;const float x=math::dot_xyz(d,right),y=math::dot_xyz(d,up),z=math::dot_xyz(d,forward);unsigned outside=0;
            if(z<camera.near_plane)outside|=1;if(z>camera.far_plane)outside|=2;if(z*half_width+x*focal<0)outside|=4;if(z*half_width-x*focal<0)outside|=8;if(z*half_height+y*focal<0)outside|=16;if(z*half_height-y*focal<0)outside|=32;common&=outside;}if(common)continue;}
        if(mesh.texture>=model.textures.size()||mesh.indices.size()%3||mesh.refraction.strength>128)throw std::runtime_error("Invalid native mesh");
        const auto flag=mesh.flags>>24;const bool translucent=!mesh.captured_scene&&(flag==0x42||flag==0x44||flag==0x48);
        const int pass=mesh.background?-1:mesh.refraction.strength?1:(mesh.effect_pass||mesh.scene_wave)?3:(!mesh.depth_write&&!mesh.world_surface)?4:translucent?2:0;
        for(std::size_t t=0;t<mesh.indices.size();t+=3){float distance=0;for(unsigned i=0;i<3;++i){if(mesh.indices[t+i]>=mesh.vertices.size())throw std::runtime_error("Native mesh index outside vertices");distance+=math::dot_xyz(mesh.vertices[mesh.indices[t+i]].position+camera.eye*-1,forward);}
            if(!std::isfinite(distance))throw std::runtime_error("Invalid native triangle position");
            if(pass==2&&mesh.layer_group){auto& layer=layers[{mesh.node,mesh.layer_group}];layer.depth+=distance;layer.corners+=3;}
            // Skinned hair uses alpha blending even for its solid texels.
            // Establish their depth before soft edges, even without refraction;
            // triangle centroids can put a near edge before a farther solid texel.
            const bool split=pass==2&&flag==0x44&&mesh.depth_write&&mesh.opaque_alpha_depth;
            if(split)append({&mesh,t,distance/3,0,false,1});
            if(mesh.refraction.strength<128)append({&mesh,t,distance/3,pass,false,split?-1:0});if(mesh.refraction.strength)append({&mesh,t,distance/3,pass,true});}
    }
    // Near-coplanar room overlays form an authored stack. Independent triangle
    // sorting lets a floor paint over half a shadow card. Sort that stack as
    // one unit, retaining packet order; glass and character triangles retain
    // ordinary sorting. Instance/node remapping keeps the group local.
    for(auto& draw:draws[3])if(draw.mesh->layer_group){const auto& layer=layers.at({draw.mesh->node,draw.mesh->layer_group});draw.distance=float(layer.depth/layer.corners);}
    // Other passes keep insertion order; only refraction and translucency sort.
    std::stable_sort(draws[2].begin(),draws[2].end(),[](const Draw& a,const Draw& b){if(a.mesh->node!=b.mesh->node)return a.mesh->node<b.mesh->node;return a.refract<b.refract;});
    std::stable_sort(draws[3].begin(),draws[3].end(),[](const Draw& a,const Draw& b){return a.distance>b.distance;});
    order_alpha_layers(std::span(draws[3]),camera.eye);
    if(result.batches.empty())result.batches.emplace_back();std::size_t batch_count=1;unsigned captured_node=~0u;bool wave_captured=false;
    const bool needs_wave=std::any_of(draws.begin(),draws.end(),[](const auto& pass){return std::any_of(pass.begin(),pass.end(),[](const Draw& d){return d.mesh->scene_wave;});});
    std::vector<Vertex> polygon,clipped;polygon.reserve(8);clipped.reserve(8);
    for(const auto& pass:draws)for(const auto& draw:pass){const auto& mesh=*draw.mesh;
        const auto& texture=textures.at(mesh.texture);const unsigned width=draw.refract?result.width:mesh.scene_wave?256:texture.width,height=draw.refract?result.height:mesh.scene_wave?256:texture.height;
        const bool captured=draw.refract||mesh.captured_scene,fog_enabled=camera.fog_enabled&&mesh.fog_mode!=0;
        const float near_fog=mesh.fog_mode==1?camera.fog_near:mesh.fog_near,far_fog=mesh.fog_mode==1?camera.fog_far:mesh.fog_far;
        if(fog_enabled&&(!std::isfinite(near_fog)||!std::isfinite(far_fog)||near_fog==far_fog))throw std::runtime_error("Invalid scene fog range");
        polygon.clear();for(unsigned i=0;i<3;++i){const auto& v=mesh.vertices[mesh.indices[draw.triangle+i]];const auto d=v.position+camera.eye*-1;float u=v.u,tv=v.v;
            if(captured){const auto displaced=d+v.normal*(draw.refract?mesh.refraction.normal_offset:mesh.normal_offset);u=math::dot_xyz(displaced,right)*focal/float(width);tv=-math::dot_xyz(displaced,up)*focal/float(height);}
            polygon.push_back({math::dot_xyz(d,right),math::dot_xyz(d,up),math::dot_xyz(d,forward),u,tv,draw.refract?mesh.refraction.tint:v.color,v.position});}
        // A reverse-wound shell crossing the near plane is not a visible
        // surface. Retaining it after clipping can otherwise turn the back of
        // a room wall into a viewport-sized occluder.
        const bool inside=std::all_of(polygon.begin(),polygon.end(),[&](const Vertex& v){return v.z-camera.near_plane>=0&&v.z*half_width+v.x*focal>=0&&v.z*half_width-v.x*focal>=0&&v.z*half_height+v.y*focal>=0&&v.z*half_height-v.y*focal>=0&&camera.far_plane-v.z>=0;});
        bool reject_near_backface=false;
        const bool room_shell=near_clip::opaque_room_shell(mesh)&&!draw.refract;
        const float shell_guard=near_clip::shell_guard(camera.near_plane,focus_distance,room_shell);
        if((!inside||shell_guard>camera.near_plane)&&std::all_of(polygon.begin(),polygon.end(),[](const Vertex& v){return v.z>0;})){
            const auto projected=[&](const Vertex& v){return math::Vector4{half_width+v.x*focal/v.z,half_height-v.y*focal/v.z,0,0};};
            const auto a=projected(polygon[0]),b=projected(polygon[1]),c=projected(polygon[2]);
            reject_near_backface=edge(a,b,c.x,c.y)<0&&std::any_of(polygon.begin(),polygon.end(),[&](const Vertex& v){return v.z<shell_guard;});
        }
        if(reject_near_backface)continue;
        // Offscreen reflective/effect meshes must not trigger a full-frame
        // capture. Publish it only when this draw emits a visible triangle,
        // still before the first ordinary/reflective draw for its node.
        const bool eye_crossing=std::any_of(polygon.begin(),polygon.end(),[](const Vertex& v){return v.z<=0;})&&std::any_of(polygon.begin(),polygon.end(),[&](const Vertex& v){return v.z>=camera.near_plane;});
        // An inside triangle passes through the clipper byte for byte.
        for(unsigned plane=0;!inside&&plane<6&&!polygon.empty();++plane){const auto distance=[&](const Vertex& v){switch(plane){case 0:return v.z-camera.near_plane;case 1:return v.z*half_width+v.x*focal;case 2:return v.z*half_width-v.x*focal;case 3:return v.z*half_height+v.y*focal;case 4:return v.z*half_height-v.y*focal;default:return camera.far_plane-v.z;}};
            clipped.clear();auto previous=polygon.back();auto d0=distance(previous);for(const auto& current:polygon){const auto d1=distance(current);if((d0>=0)!=(d1>=0))clipped.push_back(interpolate(previous,current,d0/(d0-d1)));if(d1>=0)clipped.push_back(current);previous=current;d0=d1;}polygon.swap(clipped);
            // Eye-crossing triangles have no valid original projection. Use
            // their near-clipped fan before the remaining frustum planes.
            if(plane==0&&room_shell&&eye_crossing&&near_clip::reverse_wound_polygon(polygon,half_width,half_height,focal)){reject_near_backface=true;break;}}
        if(reject_near_backface)continue;
        const auto project=[&](const Vertex& v){const float z=1/v.z;float fog=255;if(fog_enabled){const float r=1/(near_fog-far_fog);fog=std::trunc(std::clamp(v.z*(r*255)+(-far_fog*r)*255-1,0.f,255.f));}
            auto world=v.world;world.y=-world.y;world.w=1;world=math::transform(model.shadow.projection,world);if(model.shadow.depth_bias!=0)world.z+=model.shadow.depth_bias;world=world*z;
            return RasterVertex{{half_width+v.x*focal*z,half_height-v.y*focal*z,z,fog},{v.u*z+(captured?half_width/width:0),v.v*z+(captured?half_height/height:0),0,0},v.color*z,world};};
        for(std::size_t i=1;i+1<polygon.size();++i){RasterTriangle t;t.a=project(polygon[0]);t.b=project(polygon[i]);t.c=project(polygon[i+1]);const float area=edge(t.a.screen,t.b.screen,t.c.screen.x,t.c.screen.y);if(!std::isfinite(area)||std::abs(area)<.00001f)continue;
            if((draw.pass==1&&captured_node!=mesh.node)||(draw.pass==3&&needs_wave&&!wave_captured)){
                if(batch_count==result.batches.size())result.batches.emplace_back();result.batches[batch_count++].capture=true;if(draw.pass==1)captured_node=mesh.node;else wave_captured=true;
            }
            // Bits are shared with scene_raster.hlsl. Keep all material behavior,
            // including destination-alpha equations and capture ordering.
            const auto flag=mesh.flags>>24;const bool translucent=!captured&&(flag==0x42||flag==0x44||flag==0x48);
            // High-resolution ordinary surfaces use a texel-preserving
            // interpolation curve. It avoids both the former uniformly soft
            // bilinear result and nearest-neighbour blocks. Materials that
            // explicitly request linear filtering retain ordinary bilinear.
            const bool sharp_upscale=submission.high_resolution&&!captured&&!mesh.linear_filter&&!scene_texture_filter();
            const bool linear=scene_texture_filter()==2||(scene_texture_filter()==0&&mesh.linear_filter);
            unsigned bits=(captured?1:0)|(linear?2:0)|(mesh.repeat_u?4:0)|(mesh.repeat_v?8:0)|(mesh.depth_test&&!mesh.background?16:0)|(mesh.depth_write&&!translucent?32:0)|(draw.refract?64:0)|(fog_enabled?128:0)|(!draw.refract&&flag==0x48?256:0)|(!draw.refract&&flag==0x42?512:0)|(mesh.effect_blend?1024:0)|(mesh.scene_wave?2048:0)|(mesh.receives_shadow&&active_shadow?4096:0)|(sharp_upscale?8192:0);
            if(mesh.depth_write&&mesh.opaque_alpha_depth&&!draw.refract)bits|=16384;
            // Essential reflective/refraction materials keep their authored
            // scene copy at every setting. Low/Off fetch one capture texel
            // instead of four; Off also omits optional mirror geometry.
            if((draw.refract||mesh.reflection_pass)&&scene_graphics_quality().reflections!=1)bits|=262144;
            if(draw.alpha_phase>0)bits|=32768;if(draw.alpha_phase<0)bits|=65536;
            if(submission.high_resolution&&scene_graphics_quality().visual_style==4&&(mesh.world_surface||mesh.effect_pass||mesh.background))bits|=131072;
            t.texture={offsets[mesh.texture],width,height,bits};if(mesh.effect_blend)t.blend=*mesh.effect_blend;
            t.fog=mesh.fog_mode==1?camera.fog_color:mesh.fog_color;t.parameters={float(mesh.refraction.strength)/128,area,model.shadow.opacity/255,float(model.shadow.size)};result.batches[batch_count-1].triangles.push_back(t);
        }
    }
    result.batches.resize(batch_count);for(auto& batch:result.batches)bin(batch,result.width,result.height,result.tile_columns,result.tile_rows);
    // Pack after geometry so a hardware destination can allocate its complete
    // upload layout before returning a span. Never read destination contents.
    const auto words=std::max<std::size_t>(texel_count,1);std::span<unsigned> texels;
    if(destination){result.texels.clear();texels=destination(words);}
    else {result.texels.resize(words);texels=result.texels;}
    if(texels.size()!=words||!texels.data())throw std::runtime_error("Invalid native raster texel destination");
    for(unsigned i=0;i<model.textures.size();++i){const auto& texture=textures.at(i);std::memcpy(texels.data()+offsets[i],texture.rgba.data(),texture.rgba.size());}
    if(active_shadow){
        const auto count=model.shadow.mask.size(),mask_words=(count+3)/4;
        texels[cursor+mask_words-1]=0;std::memcpy(texels.data()+cursor,model.shadow.mask.data(),count);
        if(model.shadow.depth.empty())std::fill(texels.begin()+cursor+mask_words,texels.end(),std::bit_cast<unsigned>(-std::numeric_limits<float>::infinity()));
        else std::memcpy(texels.data()+cursor+mask_words,model.shadow.depth.data(),count*sizeof(float));
    }
    if(!texel_count)texels[0]=0;
}
RasterScene prepare_raster_scene(const SceneSubmission& submission,float scale,unsigned width,unsigned height){
    RasterScene result;prepare_raster_scene(result,submission,scale,width,height);return result;
}
}
