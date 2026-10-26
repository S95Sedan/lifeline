#include "rendering/renderer.hpp"
#include "rendering/near_clip.hpp"
#include "rendering/alpha_layers.hpp"
#include "rendering/enhanced.hpp"
#include <limits>
#include <execution>
#include <numeric>
#include <map>

namespace lifeline {
namespace {
class SoftwareSceneRenderer final : public SceneRenderer {
    SceneTextureCache texture_cache_;
public:
    std::string_view name()const noexcept override{return "software";}
    AssetImage render(const SceneSubmission& submission)override{
        auto size=scene_render_size();if(!submission.high_resolution||!size.first)size={640,448};
        const bool enhanced=submission.high_resolution&&scene_graphics_quality().visual_style==4;std::vector<float> depth;Bytes eligible;
        auto image=render_scene(submission.model,submission.camera,submission.visibility,submission.transparent,size.first,size.second,enhanced?&depth:nullptr,enhanced?&eligible:nullptr,&texture_cache_);
        if(submission.high_resolution){
            AssetImage source{};if(scene_graphics_quality().visual_style)source=image;
            apply_scene_anti_aliasing(image);if(enhanced)apply_scene_enhanced(image,source,depth,eligible,submission.camera);else if(scene_graphics_quality().visual_style)apply_scene_visual_style(image,source);
        }return image;
    }
};
}
std::unique_ptr<SceneRenderer> make_software_renderer(){return std::make_unique<SoftwareSceneRenderer>();}
namespace {
struct Projected {float x,y,z,u,v;math::Vector4 color;float fog=255;math::Vector4 world{};};
Projected lerp(const Projected& a,const Projected& b,float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t,a.color*(1-t)+b.color*t,255,a.world*(1-t)+b.world*t};
}
float edge(const Projected& a,const Projected& b,float x,float y) {return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
AssetImage effect_capture(const AssetImage& source){
    // 156b5c -> 1e45d0 (null source) -> 1e6de8 copies the 640x448
    // frame into the effect class's 256x256 texture before pass 8.
    AssetImage result{"Effect scene capture",256,256,Bytes(256*256*4)};
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){
        const float sx=(float(x)+.5f)*float(source.width)/256.f-.5f,sy=(float(y)+.5f)*float(source.height)/256.f-.5f;
        const auto ix=unsigned(std::max(0.f,std::floor(sx))),iy=unsigned(std::max(0.f,std::floor(sy)));
        const auto jx=std::min(ix+1,source.width-1),jy=std::min(iy+1,source.height-1);
        const float ax=sx-std::floor(sx),ay=sy-std::floor(sy);
        for(unsigned k=0;k<3;++k)result.rgba[(y*256+x)*4+k]=std::uint8_t((source.rgba[(iy*source.width+ix)*4+k]*(1-ax)+source.rgba[(iy*source.width+jx)*4+k]*ax)*(1-ay)+(source.rgba[(jy*source.width+ix)*4+k]*(1-ax)+source.rgba[(jy*source.width+jx)*4+k]*ax)*ay);
        result.rgba[(y*256+x)*4+3]=255;
    }
    return result;
}
}
AssetImage render_model(const StaticModel& model,const SceneCamera& camera,unsigned node_mask,bool transparent) {
    if(!model.nodes || model.nodes>31 || !node_mask || (node_mask>>model.nodes)!=0 || !std::isfinite(camera.focal_pixels) || camera.focal_pixels<=0 || !std::isfinite(camera.near_plane) || camera.near_plane<=0)
        throw std::runtime_error("Invalid native scene camera or node");
    std::vector<unsigned char> visible(model.nodes);for(unsigned i=0;i<model.nodes;++i)visible[i]=(node_mask>>i)&1;
    return render_scene(model,camera,visible,transparent);
}
AssetImage render_scene(const StaticModel& model,const SceneCamera& camera,std::span<const unsigned char> visibility,bool transparent,unsigned width,unsigned height,std::vector<float>* output_depth,Bytes* output_eligible,SceneTextureCache* texture_cache){
    if(!width||!height||width>7680||height>7680)throw std::runtime_error("Invalid software raster dimensions");
    const float half_width=width*.5f,half_height=height*.5f,focal=camera.focal_pixels*std::min(float(width)/640,float(height)/448);
    if(std::isnan(camera.far_plane) || camera.far_plane<=camera.near_plane)throw std::runtime_error("Invalid native camera far plane");
    if(model.nodes>1024 || (!visibility.empty() && visibility.size()!=model.nodes) || !std::isfinite(camera.focal_pixels) || camera.focal_pixels<=0 || !std::isfinite(camera.near_plane) || camera.near_plane<=0)
        throw std::runtime_error("Invalid native scene camera or visibility");
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1);
    const auto camera_right=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(camera_right,forward);
    const auto right=camera_right*(camera.mirror_horizontal?-1.f:1.f);
    const auto focus_delta=camera.target+camera.eye*-1;const float focus_distance=std::sqrt(math::dot_xyz(focus_delta,focus_delta));
    if(!std::isfinite(math::dot_xyz(forward,forward)) || !std::isfinite(math::dot_xyz(right,right))) throw std::runtime_error("Nonfinite native camera");
    AssetImage image{"Scene",width,height,Bytes(std::size_t(width)*height*4)};
    for(std::size_t i=3;i<image.rgba.size();i+=4) image.rgba[i]=transparent?0:255;
    if(!transparent && camera.clear_color.w>0)for(std::size_t i=0;i<image.rgba.size();i+=4){image.rgba[i]=std::uint8_t(std::clamp(camera.clear_color.x,0.f,255.f));image.rgba[i+1]=std::uint8_t(std::clamp(camera.clear_color.y,0.f,255.f));image.rgba[i+2]=std::uint8_t(std::clamp(camera.clear_color.z,0.f,255.f));}
    std::vector<float> depth(std::size_t(width)*height,0);
    Bytes eligible;if(output_eligible)eligible.resize(std::size_t(width)*height);
    std::vector<int> scanlines(height);std::iota(scanlines.begin(),scanlines.end(),0);
    struct Draw {const ModelMesh* mesh;std::size_t triangle;float distance;int pass;bool refract=false;int alpha_phase=0;};
    std::vector<Draw> draws;
    struct Layer {double depth=0;std::size_t corners=0;};std::map<std::pair<unsigned,unsigned>,Layer> layers;
    SceneTextures textures(model,texture_cache);
    for(const auto& mesh:model.meshes) {
        if(mesh.node>=model.nodes) throw std::runtime_error("Invalid native mesh node");
        if(!visibility.empty() && !visibility[mesh.node]) continue;
        if(mesh.reflection_pass&&!scene_graphics_quality().reflections)continue;
        if(mesh.culling_corners){
            unsigned common=63;
            for(auto point:*mesh.culling_corners){const auto delta=point+camera.eye*(-1);const float x=math::dot_xyz(delta,right),y=math::dot_xyz(delta,up),z=math::dot_xyz(delta,forward);
                unsigned outside=0;
                if(z<camera.near_plane)outside|=1;if(z>camera.far_plane)outside|=2;
                if(z*half_width+x*focal<0)outside|=4;if(z*half_width-x*focal<0)outside|=8;
                if(z*half_height+y*focal<0)outside|=16;if(z*half_height-y*focal<0)outside|=32;
                common&=outside;
            }
            if(common)continue;
        }
        if(mesh.texture>=model.textures.size() || mesh.indices.size()%3) throw std::runtime_error("Invalid native mesh");
        const auto& tex=textures.at(mesh.texture);
        if(!tex.width || !tex.height || tex.rgba.size()!=std::size_t(tex.width)*tex.height*4) throw std::runtime_error("Invalid native mesh texture");
        const bool translucent=!mesh.captured_scene && ((mesh.flags>>24)==0x42 || (mesh.flags>>24)==0x44 || (mesh.flags>>24)==0x48);
        if(mesh.refraction.strength>128)throw std::runtime_error("Invalid model refraction strength");
        const int pass=mesh.background?-1:mesh.refraction.strength?1:(mesh.effect_pass||mesh.scene_wave)?3:(!mesh.depth_write&&!mesh.world_surface)?4:translucent?2:0;
        for(std::size_t tri=0;tri<mesh.indices.size();tri+=3){float distance=0;
            for(unsigned i=0;i<3;++i){if(mesh.indices[tri+i]>=mesh.vertices.size())throw std::runtime_error("Native mesh index outside vertices");
                distance+=math::dot_xyz(mesh.vertices[mesh.indices[tri+i]].position+camera.eye*-1,forward);}
            if(!std::isfinite(distance))throw std::runtime_error("Invalid native triangle position");
            if(pass==2&&mesh.layer_group){auto& layer=layers[{mesh.node,mesh.layer_group}];layer.depth+=distance;layer.corners+=3;}
            // Solid hair texels establish depth before soft edges regardless
            // of refraction or a misleading triangle-centroid order.
            const bool split=pass==2&&(mesh.flags>>24)==0x44&&mesh.depth_write&&mesh.opaque_alpha_depth;
            if(split)draws.push_back({&mesh,tri,distance/3,0,false,1});
            if(mesh.refraction.strength<128)draws.push_back({&mesh,tri,distance/3,pass,false,split?-1:0});
            if(mesh.refraction.strength)draws.push_back({&mesh,tri,distance/3,pass,true});
        }
    }
    for(auto& draw:draws)if(draw.pass==2&&draw.mesh->layer_group){const auto& layer=layers.at({draw.mesh->node,draw.mesh->layer_group});draw.distance=float(layer.depth/layer.corners);}
    // Opaque/cutout geometry establishes depth first. Alpha-enabled triangles
    // blend back-to-front without hiding later objects behind translucent glass.
    // Coplanar CF layers are last and retain their authored painter order.
    std::stable_sort(draws.begin(),draws.end(),[](const Draw& a,const Draw& b){
        if(a.pass!=b.pass)return a.pass<b.pass;
        if(a.pass==1){
            if(a.mesh->node!=b.mesh->node)return a.mesh->node<b.mesh->node;
            return a.refract<b.refract;
        }
        return a.pass==2 && a.distance>b.distance;
    });
    const auto alpha_begin=std::find_if(draws.begin(),draws.end(),[](const Draw& d){return d.pass==2;});
    const auto alpha_end=std::find_if(alpha_begin,draws.end(),[](const Draw& d){return d.pass!=2;});
    order_alpha_layers(std::span(alpha_begin,alpha_end),camera.eye);
    AssetImage capture,wave_capture;unsigned captured_node=std::numeric_limits<unsigned>::max();
    const bool needs_wave_capture=std::any_of(draws.begin(),draws.end(),[](const Draw& draw){return draw.mesh->scene_wave;});
    std::vector<Projected> polygon,clipped;polygon.reserve(12);clipped.reserve(12);
    for(const auto& draw:draws){
        const auto& mesh=*draw.mesh;
        // Native occlusion policy for quantized near-opaque character hair;
        // not a proven PS2 alpha-test rule. Keep sampled alpha for blending.
        const float opaque_alpha_threshold=draw.alpha_phase!=0?.995f:.999f;
        // Capture before this model's ordinary pass. Partial refraction draws
        // its normal surface, then blends the displaced capture over it.
        if(draw.pass==1 && captured_node!=mesh.node){capture=image;captured_node=mesh.node;}
        if(draw.pass==3&&needs_wave_capture&&wave_capture.rgba.empty())wave_capture=effect_capture(image);
        const auto& tex=draw.refract?capture:mesh.scene_wave?wave_capture:textures.at(mesh.texture);
        const bool captured=draw.refract || mesh.captured_scene;
        const auto tri=draw.triangle;
        const bool fog_enabled=camera.fog_enabled&&mesh.fog_mode!=0;
        const float fog_near=mesh.fog_mode==1?camera.fog_near:mesh.fog_near,fog_far=mesh.fog_mode==1?camera.fog_far:mesh.fog_far;
        const auto fog_color=mesh.fog_mode==1?camera.fog_color:mesh.fog_color;
        {
            polygon.clear();
            for(unsigned i=0;i<3;++i) {
                if(mesh.indices[tri+i]>=mesh.vertices.size()) throw std::runtime_error("Native mesh index outside vertices");
                const auto& v=mesh.vertices[mesh.indices[tri+i]];const auto delta=v.position+camera.eye*-1;
                float u=v.u,tv=v.v;
                if(captured){
                    const auto displaced=delta+v.normal*(draw.refract?mesh.refraction.normal_offset:mesh.normal_offset);
                    u=math::dot_xyz(displaced,right)*focal/float(tex.width);
                    tv=-math::dot_xyz(displaced,up)*focal/float(tex.height);
                }
                polygon.push_back({math::dot_xyz(delta,right),math::dot_xyz(delta,up),math::dot_xyz(delta,forward),u,tv,draw.refract?mesh.refraction.tint:v.color,255,v.position});
            }
            bool reject_near_backface=false;
            const bool room_shell=near_clip::opaque_room_shell(mesh)&&!draw.refract;
            const float shell_guard=near_clip::shell_guard(camera.near_plane,focus_distance,room_shell);
            if(std::all_of(polygon.begin(),polygon.end(),[](const Projected& v){return v.z>0;})){
                const auto projected=[&](const Projected& v){return Projected{half_width+v.x*focal/v.z,half_height-v.y*focal/v.z};};
                const auto a=projected(polygon[0]),b=projected(polygon[1]),c=projected(polygon[2]);
                reject_near_backface=edge(a,b,c.x,c.y)<0&&std::any_of(polygon.begin(),polygon.end(),[&](const Projected& v){return v.z<shell_guard;});
            }
            if(reject_near_backface)continue;
            const bool eye_crossing=std::any_of(polygon.begin(),polygon.end(),[](const Projected& v){return v.z<=0;})&&std::any_of(polygon.begin(),polygon.end(),[&](const Projected& v){return v.z>=camera.near_plane;});
            // Clip before projection, including triangles intersecting the eye
            // plane and screen edges. Attributes travel with clipped vertices.
            for(unsigned plane=0;plane<6 && !polygon.empty();++plane) {
                const auto distance=[&](const Projected& v) {
                    switch(plane) {
                    case 0:return v.z-camera.near_plane;
                    case 1:return v.z*half_width+v.x*focal;
                    case 2:return v.z*half_width-v.x*focal;
                    case 3:return v.z*half_height+v.y*focal;
                    case 4:return v.z*half_height-v.y*focal;
                    default:return camera.far_plane-v.z;
                    }
                };
                clipped.clear();auto previous=polygon.back();auto d0=distance(previous);
                for(const auto& current:polygon) {
                    const auto d1=distance(current);
                    if((d0>=0)!=(d1>=0)) clipped.push_back(lerp(previous,current,d0/(d0-d1)));
                    if(d1>=0) clipped.push_back(current);
                    previous=current;d0=d1;
                }
                polygon.swap(clipped);
                // Match preparation's shell rule only after obtaining a valid
                // positive-near projection; preserve the clipping arithmetic.
                if(plane==0&&room_shell&&eye_crossing&&near_clip::reverse_wound_polygon(polygon,half_width,half_height,focal)){reject_near_backface=true;break;}
            }
            if(reject_near_backface)continue;
            for(auto& v:polygon) {
                if(fog_enabled){
                    if(!std::isfinite(fog_near)||!std::isfinite(fog_far)||fog_near==fog_far)throw std::runtime_error("Invalid scene fog range");
                    const float reciprocal=1.f/(fog_near-fog_far);
                    const float scale=reciprocal*255.f,bias=(-fog_far*reciprocal)*255.f-1.f;
                    v.fog=std::trunc(std::clamp(v.z*scale+bias,0.f,255.f));
                }
                v.z=1/v.z;v.x=half_width+v.x*focal*v.z;v.y=half_height-v.y*focal*v.z;
                v.u*=v.z;v.v*=v.z;v.color=v.color*v.z;v.world=v.world*v.z;
                if(captured){v.u+=half_width/float(tex.width);v.v+=half_height/float(tex.height);}
            }
            for(std::size_t t=1;t+1<polygon.size();++t) {
                const auto a=polygon[0],b=polygon[t],c=polygon[t+1];const auto area=edge(a,b,c.x,c.y);
                if(!std::isfinite(area) || std::abs(area)<.00001f) continue;
                const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(int(width)-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
                const int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(int(height)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
                if(x0>x1||y0>y1)continue;
                const bool shadowed=scene_graphics_quality().shadow_quality&&mesh.receives_shadow&&!model.shadow.mask.empty();
                const auto raster_row=[&](int y) {
                    // Restrict each scanline to the triangle's intersections,
                    // retaining a one-pixel guard for floating-point edges.
                    // Coverage/depth/interpolation below remain unchanged.
                    const float scan=float(y)+.5f;
                    float left=std::numeric_limits<float>::infinity(),right=-left;
                    const auto intersect=[&](const Projected& p,const Projected& q){
                        if(scan<std::min(p.y,q.y)||scan>std::max(p.y,q.y))return;
                        if(p.y==q.y){left=std::min(left,std::min(p.x,q.x));right=std::max(right,std::max(p.x,q.x));return;}
                        const float x=p.x+(q.x-p.x)*((scan-p.y)/(q.y-p.y));left=std::min(left,x);right=std::max(right,x);
                    };
                    intersect(a,b);intersect(b,c);intersect(c,a);
                    if(left>right)return;
                    const int begin=std::max(x0,int(std::floor(left))-1),end=std::min(x1,int(std::ceil(right))+1);
                    for(int x=begin;x<=end;++x) {
                    const float w0=edge(b,c,float(x)+.5f,float(y)+.5f)/area,w1=edge(c,a,float(x)+.5f,float(y)+.5f)/area,w2=1-w0-w1;
                    if(w0<0 || w1<0 || w2<0) continue;
                    const auto z=a.z*w0+b.z*w1+c.z*w2;const auto index=std::size_t(y)*width+x;
                    if(mesh.depth_test && !mesh.background && (draw.refract?z<depth[index]:z<=depth[index]))continue;
                    const float q=captured?1.f:z;
                    float u=(a.u*w0+b.u*w1+c.u*w2)/q,v=(a.v*w0+b.v*w1+c.v*w2)/q;
                    if(!captured){if(mesh.repeat_u)u-=std::floor(u);if(mesh.repeat_v)v-=std::floor(v);}
                    const unsigned tx=std::min(tex.width-1,unsigned(std::clamp(u,0.f,1.f)*float(tex.width))),ty=std::min(tex.height-1,unsigned(std::clamp(v,0.f,1.f)*float(tex.height)));
                    const auto* pixel=tex.rgba.data()+(ty*tex.width+tx)*4;
                    std::array<float,4> sample{float(pixel[0]),float(pixel[1]),float(pixel[2]),float(pixel[3])};
                    // Point-sampled effect sprites commonly devote most of
                    // their rectangle to fully transparent texels. Nothing
                    // below can make those pixels visible, so avoid the
                    // perspective color, fog and blend work entirely.
                    const bool linear=scene_texture_filter()==2||(scene_texture_filter()==0&&mesh.linear_filter);
                    if(!captured&&!linear&&!mesh.effect_blend&&!pixel[3])continue;
                    const bool cheap_reflection=(draw.refract||mesh.reflection_pass)&&scene_graphics_quality().reflections!=1;
                    if((captured||linear)&&!cheap_reflection){
                        // GS TEX1 linear filtering, clamp at the copied frame.
                        const bool wrap_u=!captured&&mesh.repeat_u,wrap_v=!captured&&mesh.repeat_v;
                        const float fx=wrap_u?u*float(tex.width)-.5f:std::clamp(u*float(tex.width)-.5f,0.f,float(tex.width-1));
                        const float fy=wrap_v?v*float(tex.height)-.5f:std::clamp(v*float(tex.height)-.5f,0.f,float(tex.height-1));
                        const int bx=int(std::floor(fx)),by=int(std::floor(fy));
                        const auto coord=[](int p,unsigned extent,bool wrap){return wrap?unsigned((p%int(extent)+int(extent))%int(extent)):unsigned(std::clamp(p,0,int(extent)-1));};
                        const auto ix=coord(bx,tex.width,wrap_u),iy=coord(by,tex.height,wrap_v),jx=coord(bx+1,tex.width,wrap_u),jy=coord(by+1,tex.height,wrap_v);
                        const float ax=fx-float(bx),ay=fy-float(by);
                        for(unsigned k=0;k<4;++k)sample[k]=(tex.rgba[(iy*tex.width+ix)*4+k]*(1-ax)+tex.rgba[(iy*tex.width+jx)*4+k]*ax)*(1-ay)+(tex.rgba[(jy*tex.width+ix)*4+k]*(1-ax)+tex.rgba[(jy*tex.width+jx)*4+k]*ax)*ay;
                        if(captured)sample[3]=255; // Original framebuffer texture is RGB24.
                    }
                    if(cheap_reflection&&captured)sample[3]=255;
                    const auto color=(a.color*w0+b.color*w1+c.color*w2)*(1/z);
                    const float alpha=draw.refract?float(mesh.refraction.strength)/128.f:std::clamp(sample[3]/255.f*color.w,0.f,1.f);if(alpha<=0&&!mesh.effect_blend) continue;
                    if((draw.alpha_phase>0&&alpha<opaque_alpha_threshold)||(draw.alpha_phase<0&&alpha>=opaque_alpha_threshold))continue;
                    auto* out=image.rgba.data()+index*4;
                    const float rgb[]{color.x,color.y,color.z};
                    float shadow=0;
                    if(shadowed){auto world=(a.world*w0+b.world*w1+c.world*w2)*(1/z);world.y=-world.y;world.w=1;shadow=model.shadow.sample(world);}
                    const bool additive=!draw.refract && (mesh.flags>>24)==0x48;
                    const bool subtractive=!draw.refract && (mesh.flags>>24)==0x42;
                    const float fog=(a.fog*w0+b.fog*w1+c.fog*w2)/256.f;
                    const bool contribution=(additive||subtractive)&&!mesh.effect_blend;
                    const float fog_rgb[]{contribution?0.f:fog_color.x,contribution?0.f:fog_color.y,contribution?0.f:fog_color.z};
                    for(unsigned k=0;k<3;++k){
                        float source=sample[k]*rgb[k];
                        if(fog_enabled)source=std::trunc(fog_rgb[k]+(std::clamp(source,0.f,255.f)-fog_rgb[k])*fog);
                        source*=1-shadow;
                        if(mesh.effect_blend){const auto& equation=*mesh.effect_blend;const auto value=[&](unsigned selector){return selector==0?source:selector==1?float(out[k]):0.f;};const float factor=equation[2]==0?alpha:out[3]/255.f;
                            out[k]=std::uint8_t(std::clamp((value(equation[0])-value(equation[1]))*factor+value(equation[3]),0.f,255.f));
                        }else out[k]=static_cast<unsigned char>(std::clamp((subtractive?-source:source)*alpha+out[k]*((additive||subtractive)?1.f:1-alpha),0.f,255.f));
                    }
                    out[3]=static_cast<unsigned char>(mesh.effect_blend?255*alpha:std::clamp(255*alpha+out[3]*(1-alpha),0.f,255.f));
                    const bool translucent=!captured && ((mesh.flags>>24)==0x42 || (mesh.flags>>24)==0x44 || (mesh.flags>>24)==0x48);
                    if(output_eligible)eligible[index]=alpha>=.999f&&!translucent&&mesh.depth_write&&mesh.depth_test&&!mesh.background&&!captured&&!mesh.effect_pass&&!mesh.effect_blend&&!mesh.scene_wave&&!additive&&!subtractive&&!mesh.world_surface&&(!fog_enabled||fog>=.999f);
                    if(mesh.depth_write && (!translucent || (mesh.opaque_alpha_depth&&!draw.refract&&alpha>=opaque_alpha_threshold)))depth[index]=z;
                    }
                };
                // Disjoint rows own their pixels/depth. Complete each triangle
                // before the next to preserve blending and capture ordering.
                if((x1-x0+1)*(y1-y0+1)>=16384)
                    std::for_each(std::execution::par,scanlines.begin()+y0,scanlines.begin()+y1+1,raster_row);
                else for(int y=y0;y<=y1;++y)raster_row(y);
            }
        }
    }
    if(output_depth)*output_depth=std::move(depth);if(output_eligible)*output_eligible=std::move(eligible);return image;
}
}
