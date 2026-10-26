#include "rendering/shadows.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace lifeline {
void make_shadow_map(ShadowMap& map,std::span<const ShadowCaster> casters,const ShadowSettings& settings,unsigned size){
    map.projection=math::identity();map.opacity=0;map.depth_bias=0;map.size=512;
    if(!settings.mode||casters.empty()||!size){map.mask.clear();map.depth.clear();return;}
    if(size<128||size>2048)throw std::runtime_error("Invalid shadow map size");map.size=size;
    auto minimum=casters.front().root,maximum=minimum;
    for(const auto& caster:casters){minimum={std::min(minimum.x,caster.root.x),std::min(minimum.y,caster.root.y),std::min(minimum.z,caster.root.z),1};maximum={std::max(maximum.x,caster.root.x),std::max(maximum.y,caster.root.y),std::max(maximum.z,caster.root.z),1};}
    const auto center=(minimum+maximum)*.5f,diagonal=maximum+minimum*(-1);
    const float light_length=std::hypot(settings.direction.x,settings.direction.y,settings.direction.z);
    if(!std::isfinite(light_length)||light_length==0){map.mask.clear();map.depth.clear();return;}
    const math::Vector4 z{settings.direction.x/light_length,settings.direction.y/light_length,settings.direction.z/light_length,0};
    auto x=math::normalize_xyz(math::cross_xyz(z,{0,-1,0,0}));
    if(math::dot_xyz(x,x)==0)x=math::normalize_xyz(math::cross_xyz(z,{1,0,0,0}));
    const auto y=math::normalize_xyz(math::cross_xyz(x,z));
    const auto finite=[](math::Vector4 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.w);};
    float extent=std::hypot(diagonal.x,diagonal.y,diagonal.z)*.5f+15.f;
    if(!finite(center)||!std::isfinite(extent)){map.mask.clear();map.depth.clear();return;}
    // Retain the root-derived minimum, but fit the actual world-space posed
    // vertices. An extended limb must not reject a whole triangle at the edge.
    for(const auto& caster:casters)for(const auto& vertex:caster.vertices){
        if(!finite(vertex))continue;
        const auto relative=vertex+center*(-vertex.w);
        extent=std::max({extent,std::abs(math::dot_xyz(x,relative)),std::abs(math::dot_xyz(y,relative))});
    }
    // One world unit of padding and two-unit radius steps avoid continuously
    // resizing the projection for small pose changes. Keep the existing center
    // and 1/16-texel raster snap below.
    const float radius=std::ceil((extent+1.f)/2.f)*2.f;
    if(!std::isfinite(radius)){map.mask.clear();map.depth.clear();return;}
    map.projection=math::transpose(math::Matrix4{{x*(1/radius),y*(1/radius),z*(1/radius),{0,0,0,1}}});
    map.projection.columns[3]=math::transform(map.projection,center)*(-1);map.projection.columns[3].w=1;
    map.depth_bias=.025f/radius;
    map.opacity=std::clamp(settings.opacity/128.f,0.f,1.f);map.mask.assign(std::size_t(size)*size,0);
    map.depth.assign(std::size_t(size)*size,std::numeric_limits<float>::infinity());
    const auto edge=[](math::Vector4 a,math::Vector4 b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
    std::vector<math::Vector4> positions;
    for(const auto& caster:casters){
        if(caster.indices.size()%3)throw std::runtime_error("Invalid shadow triangle list");
        positions.clear();positions.reserve(caster.vertices.size());for(const auto& v:caster.vertices){auto p=math::transform(map.projection,v);positions.push_back(p);}
        for(std::size_t i=0;i<caster.indices.size();i+=3){
            auto a=positions.at(caster.indices[i]),b=positions.at(caster.indices[i+1]),c=positions.at(caster.indices[i+2]);
            // Bounds include finite posed vertices. Clip raster bounds instead
            // of dropping an entire triangle if rounding puts a vertex outside XY.
            if(!finite(a)||!finite(b)||!finite(c))continue;
            const float half=size*.5f;for(auto* p:{&a,&b,&c}){p->x=std::trunc((half+p->x*half)*16)/16;p->y=std::trunc((half+p->y*half)*16)/16;}
            const float area=edge(a,b,c.x,c.y);if(std::abs(area)<1e-7f)continue;
            const int left=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),right=std::min(int(size)-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
            const int top=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),bottom=std::min(int(size)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
            for(int py=top;py<=bottom;++py)for(int px=left;px<=right;++px){const float u=edge(b,c,px+.5f,py+.5f)/area,v=edge(c,a,px+.5f,py+.5f)/area;if(u>=0&&v>=0&&u+v<=1){const auto at=py*size+px;map.mask[at]=255;map.depth[at]=std::min(map.depth[at],a.z*u+b.z*v+c.z*(1-u-v));}}
        }
    }
}
ShadowMap make_shadow_map(std::span<const ShadowCaster> casters,const ShadowSettings& settings,unsigned size){ShadowMap map;make_shadow_map(map,casters,settings,size);return map;}
float ShadowMap::sample(math::Vector4 world)const{
    if(mask.empty())return 0;const auto p=math::transform(projection,world);
    const float x=(p.x+1)*(size*.5f)-.5f,y=(p.y+1)*(size*.5f)-.5f;
    if(!std::isfinite(x)||!std::isfinite(y)||x<=-1||x>=size||y<=-1||y>=size)return 0;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const float ax=x-ix,ay=y-iy;
    // Outside this finite projection there is no caster coverage. Clamping to
    // an occupied edge texel extrudes a shadow across unrelated room surfaces.
    const auto tap=[&](int u,int v){
        if(u<0||u>=int(size)||v<0||v>=int(size))return 0.f;
        const auto at=v*size+u;
        if(!depth.empty() && (depth.size()!=mask.size() || !(p.z+depth_bias>=depth[at])))return 0.f;
        return float(mask[at]);
    };
    return ((tap(ix,iy)*(1-ax)+tap(ix+1,iy)*ax)*(1-ay)+(tap(ix,iy+1)*(1-ax)+tap(ix+1,iy+1)*ax)*ay)*(opacity/255.f);
}
}
