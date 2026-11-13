#include "rendering/lighting.hpp"
#include "rendering/model.hpp"
#include "rendering/enhanced.hpp"
#include "rendering/quality.hpp"
#include "assets/asset_directory.hpp"
#include <bit>
#include <algorithm>
#include <cmath>

namespace lifeline {
namespace {
struct Reader {
    std::span<const unsigned char> bytes;
    void check(std::size_t p,std::size_t n)const{if(p>bytes.size() || n>bytes.size()-p)throw std::runtime_error("Scene light record outside model");}
    std::uint32_t word(std::size_t p)const{check(p,4);return le32(bytes.data()+p);}
    int integer(std::size_t p)const{return std::bit_cast<int>(word(p));}
    float number(std::size_t p)const{const float v=std::bit_cast<float>(word(p));if(!std::isfinite(v))throw std::runtime_error("Nonfinite scene light value");return v;}
    math::Vector4 vector(std::size_t p,float w=0)const{return {number(p),number(p+4),number(p+8),w};}
    int short_integer(std::size_t p)const{check(p,2);return std::bit_cast<std::int16_t>(std::uint16_t(le16(bytes.data()+p)));}
};
}
float SceneLight::intensity(float distance)const{
    if(!std::isfinite(distance)||distance<0)throw std::runtime_error("Invalid scene light distance");
    // 1edd40: polynomial coefficients are reciprocals of authored values.
    if(attenuation==0)return distance==0?1.f:std::min(1.f,falloff.x/distance);
    const float edge=attenuation==2?(range==0?(distance==0?1.f:0.f):std::clamp(1-distance/range,0.f,1.f)):1.f;
    const auto reciprocal=[](float value){return value<=0?0.f:1/value;};
    float divisor=reciprocal(falloff.x)+reciprocal(falloff.y)*distance;
    divisor+=reciprocal(falloff.z)*distance*distance;
    divisor+=reciprocal(falloff.w)*distance*distance*distance;
    return divisor<=0?0.f:edge/divisor;
}
std::vector<SceneLight> decode_scene_lights(std::span<const unsigned char> bytes){
    Reader r{bytes};r.check(0,64);const std::string version(bytes.begin(),bytes.begin()+10);
    if(version!="SOLCOM0418" && version!="SOLCOM0419")throw std::runtime_error("Unsupported scene light model version");
    const unsigned count=r.word(36),offset=r.word(32),stride=version=="SOLCOM0418"?32:128;
    r.check(offset,std::size_t(count)*stride);
    std::vector<SceneLight> result;result.reserve(std::min(count,512u));
    for(unsigned i=0;i<std::min(count,512u);++i){
        const auto p=std::size_t(offset)+i*stride;SceneLight light;
        if(stride==128){
            light.type=unsigned(std::clamp(r.integer(p+80),0,4));
            light.group=unsigned(std::clamp(r.integer(p+88),0,15));
            light.shading=unsigned(std::clamp(r.integer(p+92),0,2));
            light.attenuation=unsigned(std::clamp(r.integer(p+100),0,2));
            light.rotation_order=unsigned(std::clamp(std::int64_t(r.integer(p+104))-1,std::int64_t(0),std::int64_t(5)));
            light.color=r.vector(p)*(light.attenuation==0?255.f:128.f);
            light.position=r.vector(p+16,1);light.rotation=r.vector(p+32);light.scale=r.vector(p+48);
            light.falloff={r.number(p+64),r.number(p+68),r.number(p+72),r.number(p+76)};
            light.range=light.scale.x;light.cone=r.number(p+108);light.spread=r.number(p+112);
            light.exponent=r.number(p+116);light.bias=r.number(p+120)*.01f;light.gain=r.number(p+124)*.01f+1;
        }else{
            light.type=(bytes[p+15]&15)?3:0;light.group=bytes[p+15]>>4;
            light.shading=light.type?1:0;light.attenuation=0;
            light.color={float(bytes[p]),float(bytes[p+1]),float(bytes[p+2]),0};
            light.position=r.vector(p+16,1);light.falloff.x=r.number(p+28);
            light.bias=float(r.short_integer(p+4))*.01f;
            light.gain=float(r.short_integer(p+6)+100)*.01f;
        }
        result.push_back(light);
    }
    return result;
}
void SceneLights::load(std::span<const unsigned char> model){
    auto records=decode_scene_lights(model);
    // 1e2030 leaves the active lights alone when a model has no light table.
    replace(records);
}
void SceneLights::translate(math::Vector4 delta){
    if(!std::isfinite(delta.x)||!std::isfinite(delta.y)||!std::isfinite(delta.z))throw std::runtime_error("Invalid scene light translation");
    delta.w=0;for(auto& light:records_)light.position=light.position+delta;
}
std::vector<unsigned> SceneLights::enabled()const{
    std::vector<unsigned> result;
    for(unsigned i=0;i<records_.size();++i)if((groups_>>records_[i].group)&1)result.push_back(i);
    return result;
}
}


#include "rendering/lighting.hpp"
#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace lifeline {
namespace {
using math::Vector4;
Vector4 product(Vector4 a,Vector4 b){return {a.x*b.x,a.y*b.y,a.z*b.z,0};}
Vector4 bytes(Vector4 a){return {std::trunc(a.x),std::trunc(a.y),std::trunc(a.z),0};}
float cosine(float angle){
    constexpr float tau=2*std::numbers::pi_v<float>;
    const auto turn=std::int32_t(angle/tau*65536.f);
    return std::cos(float(turn&65535)*(tau/65536.f));
}
Vector4 rotate(const SceneLight& light,Vector4 v){
    // 1cb2f0's six rotation orders.
    constexpr unsigned order[6][3]={{2,1,0},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{0,1,2}};
    const float angles[]{light.rotation.x,light.rotation.y,light.rotation.z};
    for(unsigned j=0;j<3;++j){const unsigned axis=order[light.rotation_order][2-j];
        const float a=angles[axis],c=cosine(a),s=cosine(a-std::numbers::pi_v<float>/2);
        if(axis==0)v={v.x,c*v.y-s*v.z,s*v.y+c*v.z,0};
        else if(axis==1)v={c*v.x+s*v.z,v.y,-s*v.x+c*v.z,0};
        else v={c*v.x-s*v.y,s*v.x+c*v.y,v.z,0};
    }return v;
}
Vector4 direction(const SceneLight& light){return rotate(light,{0,0,-1,0});}
struct Selected {const SceneLight* light;Vector4 direction,light_direction;float distance,planar_response=1;};
}
static void light_meshes(std::span<ModelMesh> meshes,std::span<const SceneLight> lights,std::uint32_t groups,
                 Vector4 position,Vector4 camera_forward,Vector4 bias,bool actor,Vector4 fill,bool surface_y_down=false){
    const bool enhanced=actor&&scene_graphics_quality().visual_style==4;
    const bool readable=actor&&scene_graphics_quality().visual_style==3;
    EnhancedLights extra{};if(enhanced)extra=enhanced_lights(camera_forward);
    // The original four-source skin limit makes whole lights disappear when
    // an actor moves or a weak spotlight crosses its cone. Native shading
    // uses every enabled source. Keep authored forced fill and static-node
    // selection unchanged; neither needs the ordinary character light budget.
    const unsigned limit=actor&&fill.w<=0?unsigned(lights.size()):(actor?4u:8u);
    std::array<Selected,8> fixed_selected{};std::vector<Selected> extra_selected;
    if(limit>fixed_selected.size())extra_selected.resize(std::size_t(limit)+1);
    const auto selected=extra_selected.empty()?std::span<Selected>(fixed_selected):std::span<Selected>(extra_selected);
    Vector4 ambient{};unsigned selected_count=0;
    for(const auto& light:lights){
        if(!((groups>>light.group)&1))continue;
        auto delta=position+light.position*(-1);float distance=std::sqrt(math::dot_xyz(delta,delta));
        if(light.type==0){ambient=bytes(light.color*(light.attenuation?light.falloff.x:1.f));continue;}
        const auto light_direction=direction(light);
        auto ray=light.type==1?light_direction:math::normalize_xyz(delta);float planar_response=1;
        if(light.type==4){
            // 1ec654..1ec6f4 and 1ed61c..1ed8a8: type 4 is a one-sided
            // rectangular area light.  The original transforms four
            // [-1,1] plane corners, measures point-to-rectangle distance and
            // averages the normalized corner rays used by the VU shader.
            const auto normal=light_direction;
            if(math::dot_xyz(delta,normal)<0)continue;
            const auto axis_x=rotate(light,{1,0,0,0}),axis_y=rotate(light,{0,1,0,0});
            const float half_x=std::abs(light.scale.x),half_y=std::abs(light.scale.y);
            const float x=std::clamp(math::dot_xyz(delta,axis_x),-half_x,half_x);
            const float y=std::clamp(math::dot_xyz(delta,axis_y),-half_y,half_y);
            const auto closest=light.position+axis_x*x+axis_y*y;
            const auto closest_delta=position+closest*(-1);
            distance=std::sqrt(math::dot_xyz(closest_delta,closest_delta));
            ray={};
            for(const float corner_x:{-half_x,half_x})for(const float corner_y:{-half_y,half_y}){
                const auto corner=light.position+axis_x*corner_x+axis_y*corner_y;
                ray=ray+math::normalize_xyz(position+corner*(-1));
            }
            ray=math::normalize_xyz(ray);
            planar_response=std::clamp(math::dot_xyz(normal,ray),0.f,1.f);
            if(planar_response==0)continue;
        }
        if(light.attenuation==2 && light.type>=2 && distance>light.range)continue;
        if(light.type==2 && math::dot_xyz(ray,light_direction)<cosine(light.spread*.5f+std::max(0.f,light.cone)))continue;
        Selected candidate{&light,ray,light_direction,distance,planar_response};
        if(selected_count<limit)selected[selected_count++]=candidate;
        else {auto farthest=std::max_element(selected.begin(),selected.begin()+selected_count,[](auto& a,auto& b){return a.distance<b.distance;});if(distance<farthest->distance)*farthest=candidate;}
    }
    // 1ec3b4..1ec518: a camera-facing diffuse light fills an empty set,
    // or is explicitly forced by camera +18c. A full set replaces slot zero.
    SceneLight fallback;fallback.type=1;fallback.shading=1;fallback.attenuation=1;
    fallback.color=fill;fallback.bias=-1;fallback.gain=1;
    if(fill.w>0 || !selected_count){
        const auto ray=math::normalize_xyz(camera_forward);Selected entry{&fallback,ray,ray,0,1};
        if(selected_count<(actor?4u:8u))selected[selected_count++]=entry;else selected[0]=entry;
    }
    struct Contribution {Vector4 ray,color;float attenuation,upper,lower;};
    std::array<Contribution,8> fixed_contributions;std::vector<Contribution> extra_contributions;
    if(selected_count>fixed_contributions.size())extra_contributions.resize(selected_count);
    const auto contributions=extra_contributions.empty()?std::span<Contribution>(fixed_contributions):std::span<Contribution>(extra_contributions);
    Vector4 enhanced_key{},enhanced_half{},enhanced_key_color{};bool enhanced_key_ready=false;
    for(auto& mesh:meshes){
        if(mesh.vertices.empty())continue;
        const auto material=actor?mesh.skin_material:SkinMaterial{};
        const auto base=product(material.ambient_extra+Vector4{1,1,1,0},material.diffuse);
        const auto constant=product(base,ambient)+material.emission+bias;
        unsigned contribution_count=0;
        Vector4 energy=constant,key_sum{},key_color{};float key_weight=0;
        for(const auto& item:std::span(selected).first(selected_count)){
                const auto& light=*item.light;auto ray=item.direction;
                // Static upload 1ed010 zeros non-diffuse light colors.
                if(!actor && light.shading!=1)continue;
                float attenuation=light.type==1?(light.attenuation==1?light.falloff.x:1.f):light.intensity(item.distance);
                if(light.type==4)attenuation*=item.planar_response;
                if(light.type==2){
                    const float dot=math::dot_xyz(ray,item.light_direction);
                    const float inner=cosine(light.spread*.5f+std::min(0.f,light.cone));
                    const float outer=cosine(light.spread*.5f+std::max(0.f,light.cone));
                    const float cone=dot>inner?1.f:dot>outer?(dot-outer)/(inner-outer):0.f;
                    attenuation*=cone*(light.exponent>0?std::pow(std::max(0.f,dot),light.exponent):1.f);
                }
                if(light.shading==2)ray=math::normalize_xyz(ray+camera_forward);
                const bool diffuse=light.shading==1;
                const float upper=diffuse?light.gain:1.f;
                const float lower=diffuse?light.bias:1.f-(128*(1-(material.power-2)/98))/(100*((material.power-2)/98)+1)/128;
                // Skin specular is authored in the same 0..128 modulation
                // domain as diffuse. Applying it as an additional full-scale
                // host color doubles bright guest-room materials before the
                // GS-style clamp and washes out Rio's blouse and skin.
                const auto coefficient=diffuse?base*material.diffuse_gain:material.specular*.5f;
                contributions[contribution_count++]={ray,product(bytes(light.color),coefficient),attenuation,upper,lower};
                if(readable)energy=energy+product(bytes(light.color),coefficient)*std::max(0.f,attenuation);
                if(enhanced){const auto intensity=product(bytes(light.color),coefficient)*attenuation;energy=energy+intensity;if(diffuse&&item.light!=&fallback&&!enhanced_key_ready){const auto illumination=bytes(light.color)*attenuation;const float weight=std::max(0.f,illumination.x*.299f+illumination.y*.587f+illumination.z*.114f);key_sum=key_sum+ray*weight;key_weight+=weight;key_color=key_color+illumination;}}
        }
        const bool enhanced_specular=enhanced&&(material.specular.x>0||material.specular.y>0||material.specular.z>0);if(enhanced){if(!enhanced_key_ready){const auto fallback_key=extra.key*-1;auto authored_key=key_sum*(1/std::max(32.f,key_weight));authored_key=authored_key+fallback_key*(-std::min(0.f,math::dot_xyz(authored_key,fallback_key)));enhanced_key=math::normalize_xyz(fallback_key+authored_key*.8f);enhanced_half=math::normalize_xyz(enhanced_key+camera_forward);enhanced_key_color=key_color;enhanced_key_ready=true;}energy={std::clamp(energy.x,0.f,128.f),std::clamp(energy.y,0.f,128.f),std::clamp(energy.z,0.f,128.f),0};}
        for(auto& vertex:mesh.vertices){
            auto normal=vertex.normal;if(surface_y_down)normal.y=-normal.y; // live actor skin surface is Y-down; authored lighting is Y-up.
            auto color=constant;
            for(const auto& item:std::span(contributions).first(contribution_count)){
                const float response=item.upper==item.lower?0.f:std::max(0.f,(math::dot_xyz(normal,item.ray)-item.lower)/(item.upper-item.lower));
                color=color+item.color*(item.attenuation*response);
            }
            if(enhanced&&math::dot_xyz(normal,normal)>1e-12f){
                const auto n=math::normalize_xyz(normal);const auto original=color,emission=constant;
                const float diffuse=math::dot_xyz(n,enhanced_key),shape=diffuse>=0?1+.3f*diffuse:1+.42f*diffuse,facing=std::clamp(math::dot_xyz(n,camera_forward),0.f,1.f),rim=(1-facing)*(1-facing)*(.6f+.4f*std::clamp(-n.y,0.f,1.f));
                const auto edge=energy*(.16f*rim),specular=enhanced_specular?product(enhanced_key_color,material.specular)*(.15f*std::pow(std::max(0.f,math::dot_xyz(n,enhanced_half)),std::clamp(material.power*.5f,6.f,24.f))):Vector4{};
                const auto component=[&](float value,float emit,float e,float r,float s){return std::min(emit+(value-emit)*shape+std::min(r,18.f)+std::clamp(s,0.f,20.f),1.3f*value+.25f*e);};
                color={component(original.x,emission.x,energy.x,edge.x,specular.x),component(original.y,emission.y,energy.y,edge.y,specular.y),component(original.z,emission.z,energy.z,edge.z,specular.z),color.w};
            }
            if(readable&&math::dot_xyz(normal,normal)>1e-12f){
                // Modern's broad camera fill restores readable backlit faces.
                // Derive its energy from selected authored lights/materials,
                // cap it at 14/128, and leave already-lit values untouched.
                const float facing=std::clamp(math::dot_xyz(math::normalize_xyz(normal),camera_forward),0.f,1.f);
                const auto fill_component=[&](float value,float available){return std::max(value,std::min(14.f,std::clamp(available,0.f,128.f)*.18f)*facing);};
                color.x=fill_component(color.x,energy.x);color.y=fill_component(color.y,energy.y);color.z=fill_component(color.z,energy.z);
            }
            if(actor){
                // All enabled sources remain continuous, but their overlap can
                // wash characters out against the room's baked lighting. Keep
                // low illumination intact and roll bright peaks toward 100/128.
                // One RGB scale preserves the authored light colour instead of
                // compressing each channel toward the same pale highlight.
                const float peak=std::max({0.f,color.x,color.y,color.z});
                if(peak>64.f){
                    const float highlight=64.f+36.f*(1-std::exp(-(peak-64.f)/36.f));
                    color={(color.x/peak)*highlight,(color.y/peak)*highlight,(color.z/peak)*highlight,color.w};
                }
            }
            // Convert the recovered 0..128 modulation domain to renderer RGB;
            // the shared highlight shoulder above remains below unity while
            // static geometry retains its independently authored colors.
            vertex.color.x=std::trunc(std::clamp(color.x,0.f,128.f))/128;
            vertex.color.y=std::trunc(std::clamp(color.y,0.f,128.f))/128;
            vertex.color.z=std::trunc(std::clamp(color.z,0.f,128.f))/128;
        }
    }
}
void light_actor(StaticModel& model,std::span<const SceneLight> lights,std::uint32_t groups,
                 Vector4 position,Vector4 camera_forward,Vector4 bias,Vector4 fill){
    light_meshes(model.meshes,lights,groups,position,camera_forward,bias,true,fill,true);
}
void light_character(StaticModel& model,std::span<const SceneLight> lights,std::uint32_t groups,
                 Vector4 position,Vector4 camera_forward,Vector4 fill){
    light_meshes(model.meshes,lights,groups,position,camera_forward,{},true,fill,false);
}
void light_node(ModelMesh& mesh,std::span<const SceneLight> lights,std::uint32_t groups,Vector4 camera_forward,Vector4 fill){
    light_meshes({&mesh,1},lights,groups,mesh.lighting_position,camera_forward,{},false,fill);
}
}
