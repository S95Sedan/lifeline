#include "rendering/quality.hpp"
#include "rendering/model.hpp"
#include <algorithm>
#include <array>
#include <execution>
#include <limits>
#include <numeric>
#include <utility>

namespace lifeline {
unsigned shadow_resolution(unsigned quality){if(quality>4)throw std::runtime_error("Invalid shadow quality");return quality?128u<<quality:0;}
namespace {
AssetImage reduce_texture(const AssetImage& source,unsigned quality){
    const unsigned factor=quality==0?4:2,width=std::max(1u,(source.width+factor-1)/factor),height=std::max(1u,(source.height+factor-1)/factor);
    AssetImage target{source.name,width,height,Bytes(std::size_t(width)*height*4)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        std::array<unsigned,4> sum{};unsigned count=0;
        for(unsigned v=y*factor;v<std::min((y+1)*factor,source.height);++v)for(unsigned u=x*factor;u<std::min((x+1)*factor,source.width);++u){
            const auto* p=source.rgba.data()+(std::size_t(v)*source.width+u)*4;
            for(unsigned k=0;k<3;++k)sum[k]+=unsigned(p[k])*p[3];sum[3]+=p[3];++count;
        }
        auto* p=target.rgba.data()+(std::size_t(y)*width+x)*4;
        for(unsigned k=0;k<3;++k)p[k]=sum[3]?static_cast<unsigned char>((sum[k]+sum[3]/2)/sum[3]):0;
        p[3]=static_cast<unsigned char>((sum[3]+count/2)/count);
    }
    return target;
}
}
SceneTextureCache::SceneTextureCache(const SceneTextureCache& other):budget_(other.budget_){}
SceneTextureCache& SceneTextureCache::operator=(const SceneTextureCache& other){
    if(this!=&other){entries_.clear();hints_.clear();bytes_=0;age_=0;budget_=other.budget_;}return *this;
}
SceneTextureCache::SceneTextureCache(SceneTextureCache&& other)noexcept
    :entries_(std::move(other.entries_)),hints_(std::move(other.hints_)),bytes_(std::exchange(other.bytes_,0)),budget_(other.budget_),age_(std::exchange(other.age_,0)){}
SceneTextureCache& SceneTextureCache::operator=(SceneTextureCache&& other)noexcept{
    if(this!=&other){entries_=std::move(other.entries_);hints_=std::move(other.hints_);bytes_=std::exchange(other.bytes_,0);budget_=other.budget_;age_=std::exchange(other.age_,0);}return *this;
}
void SceneTextureCache::begin(std::size_t count){
    hints_.resize(count,std::numeric_limits<std::size_t>::max());
    for(auto& entry:entries_)if(entry)entry->pinned=false;
}
const AssetImage* SceneTextureCache::reduced(const AssetImage& source,unsigned index,unsigned quality){
    if(quality>1)throw std::runtime_error("Invalid cached texture quality");
    auto& hint=hints_.at(index);Entry* entry=nullptr;std::size_t slot=hint;
    const auto matches=[&](std::size_t i){const auto& p=entries_[i];return p&&p->source.width==source.width&&p->source.height==source.height&&p->source.name==source.name&&p->source.rgba==source.rgba;};
    if(hint<entries_.size()&&matches(hint))entry=entries_[hint].get();
    else for(std::size_t i=0;i<entries_.size();++i)if(i!=hint&&matches(i)){slot=i;entry=entries_[i].get();break;}
    if(entry&&!entry->levels[quality].rgba.empty()){hint=slot;entry->age=++age_;entry->pinned=true;return &entry->levels[quality];}
    // Replace a changed page at its old hint instead of retaining every frame.
    if(!entry&&hint<entries_.size()&&entries_[hint]&&!entries_[hint]->pinned){
        const auto& old=entries_[hint]->source;
        if(old.name==source.name&&old.width==source.width&&old.height==source.height){bytes_-=entries_[hint]->bytes;entries_[hint].reset();}
    }
    const unsigned factor=quality==0?4:2,width=std::max(1u,(source.width+factor-1)/factor),height=std::max(1u,(source.height+factor-1)/factor);
    const std::size_t needed=std::size_t(width)*height*4+(entry?0:source.rgba.size());
    if(needed>budget_)return nullptr;
    while(needed>budget_-bytes_){
        std::size_t oldest=entries_.size();
        for(std::size_t i=0;i<entries_.size();++i)if(entries_[i]&&entries_[i].get()!=entry&&!entries_[i]->pinned&&(oldest==entries_.size()||entries_[i]->age<entries_[oldest]->age))oldest=i;
        if(oldest==entries_.size())return nullptr;
        bytes_-=entries_[oldest]->bytes;entries_[oldest].reset();
    }
    auto level=reduce_texture(source,quality);
    if(!entry){
        auto replacement=std::make_unique<Entry>();replacement->source=source;
        slot=0;while(slot<entries_.size()&&entries_[slot])++slot;
        if(slot==entries_.size())entries_.push_back(nullptr);
        entry=replacement.get();entries_[slot]=std::move(replacement);
    }
    entry->levels[quality]=std::move(level);entry->bytes+=needed;bytes_+=needed;
    hint=slot;entry->age=++age_;entry->pinned=true;return &entry->levels[quality];
}
SceneTextures::SceneTextures(const StaticModel& model,SceneTextureCache* cache):model_(model){
    const auto quality=scene_graphics_quality().texture_quality;if(quality==2)return;
    std::vector<bool> ordinary(model.textures.size()),protected_page(model.textures.size());
    for(const auto& mesh:model.meshes){
        if(mesh.texture>=model.textures.size())throw std::runtime_error("Invalid quality texture index");
        const bool protect=mesh.world_surface||mesh.effect_pass||mesh.background||!mesh.depth_test||mesh.captured_scene||mesh.scene_wave;
        (protect?protected_page:ordinary)[mesh.texture]=true;
    }
    reduced_.resize(model.textures.size());if(cache){cache->begin(model.textures.size());cached_.resize(model.textures.size());}
    for(unsigned i=0;i<model.textures.size();++i){
        if(!ordinary[i]||protected_page[i])continue;const auto& source=model.textures[i];
        if(!source.width||!source.height||source.rgba.size()!=std::size_t(source.width)*source.height*4)throw std::runtime_error("Invalid quality texture");
        if(cache){cached_[i]=cache->reduced(source,i,quality);if(cached_[i])continue;}
        reduced_[i]=reduce_texture(source,quality);
    }
}
const AssetImage& SceneTextures::at(unsigned i)const{
    if(!cached_.empty()&&cached_.at(i))return *cached_[i];
    return !reduced_.empty()&&!reduced_.at(i).rgba.empty()?reduced_[i]:model_.textures.at(i);
}
void apply_scene_anti_aliasing(AssetImage& image){
    if(!scene_graphics_quality().anti_aliasing||image.width<3||image.height<3)return;
    const auto source=image.rgba;const int width=int(image.width),height=int(image.height);
    const auto texel=[&](int x,int y){const auto* p=source.data()+(std::size_t(std::clamp(y,0,height-1))*width+std::clamp(x,0,width-1))*4;return math::Vector4{float(p[0]),float(p[1]),float(p[2]),float(p[3])};};
    const auto sample=[&](float x,float y){const int ix=int(std::floor(x)),iy=int(std::floor(y));const float u=x-ix,v=y-iy;return (texel(ix,iy)*(1-u)+texel(ix+1,iy)*u)*(1-v)+(texel(ix,iy+1)*(1-u)+texel(ix+1,iy+1)*u)*v;};
    const auto luma=[](math::Vector4 p){return p.x*.299f+p.y*.587f+p.z*.114f;};
    std::vector<int> rows(height);std::iota(rows.begin(),rows.end(),0);
    // Spatial FXAA: follow the local luminance edge, then choose the wider
    // sample only when it stays within the neighbourhood's luminance range.
    std::for_each(std::execution::par,rows.begin(),rows.end(),[&](int y){for(int x=0;x<width;++x){
        const auto m=texel(x,y),nw=texel(x-1,y-1),ne=texel(x+1,y-1),sw=texel(x-1,y+1),se=texel(x+1,y+1);
        if(std::min({m.w,nw.w,ne.w,sw.w,se.w})<254)continue;
        const float lm=luma(m),lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se);
        const float low=std::min({lm,lnw,lne,lsw,lse}),high=std::max({lm,lnw,lne,lsw,lse});if(high-low<std::max(8.f,high*.125f))continue;
        float dx=-((lnw+lne)-(lsw+lse)),dy=(lnw+lsw)-(lne+lse);
        const float reduce=std::max((lnw+lne+lsw+lse)*(.25f*.125f),255.f/128),inverse=1/(std::min(std::abs(dx),std::abs(dy))+reduce);
        dx=std::clamp(dx*inverse,-8.f,8.f);dy=std::clamp(dy*inverse,-8.f,8.f);
        const auto a=(sample(x-dx/6,y-dy/6)+sample(x+dx/6,y+dy/6))*.5f;
        const auto b=a*.5f+(sample(x-dx*.5f,y-dy*.5f)+sample(x+dx*.5f,y+dy*.5f))*.25f;
        const float lb=luma(b);const auto result=lb<low||lb>high?a:b;auto* out=image.rgba.data()+(std::size_t(y)*width+x)*4;
        out[0]=static_cast<unsigned char>(std::clamp(std::lround(result.x),0l,255l));out[1]=static_cast<unsigned char>(std::clamp(std::lround(result.y),0l,255l));out[2]=static_cast<unsigned char>(std::clamp(std::lround(result.z),0l,255l));
    }});
}
void apply_scene_visual_style(AssetImage& image,const AssetImage& source,std::span<const unsigned char> eligible){
    const auto selected=scene_graphics_quality().visual_style,style=selected==4?2:std::min(selected,3u);if(!style)return;
    if(!image.width||!image.height||image.width!=source.width||image.height!=source.height||image.rgba.size()!=std::size_t(image.width)*image.height*4||source.rgba.size()!=image.rgba.size()||image.rgba.data()==source.rgba.data())throw std::runtime_error("Invalid visual style source");
    const int width=int(image.width),height=int(image.height),strength=int(style),limit=style==1?6:style==2?10:14;
    if(!eligible.empty()&&eligible.size()!=std::size_t(width)*height)throw std::runtime_error("Invalid visual style mask");
    const auto pixel=[&](int x,int y){return source.rgba.data()+(std::size_t(std::clamp(y,0,height-1))*width+std::clamp(x,0,width-1))*4;};
    const auto luma=[](const unsigned char* p){return (77*int(p[0])+150*int(p[1])+29*int(p[2])+128)/256;};
    std::vector<int> rows(height);std::iota(rows.begin(),rows.end(),0);
    std::for_each(std::execution::par,rows.begin(),rows.end(),[&](int y){for(int x=0;x<width;++x){
        if(!eligible.empty()&&!eligible[std::size_t(y)*width+x])continue;
        const auto *m=pixel(x,y),*l=pixel(x-1,y),*r=pixel(x+1,y),*u=pixel(x,y-1),*d=pixel(x,y+1);
        if(std::min({m[3],l[3],r[3],u[3],d[3]})<254)continue;
        const int low=std::min({luma(m),luma(l),luma(r),luma(u),luma(d)}),high=std::max({luma(m),luma(l),luma(r),luma(u),luma(d)}),weight=64-std::min(high-low,64);
        auto* out=image.rgba.data()+(std::size_t(y)*width+x)*4;std::array<int,3> rgb;
        for(unsigned k=0;k<3;++k){const int detail=(4*int(m[k])-int(l[k])-int(r[k])-int(u[k])-int(d[k]))*strength*weight/2048;int v=std::clamp(int(out[k])+std::clamp(detail,-limit,limit),0,255);
            if(style>=2){const int lift=style==2?2:3,contrast=style==2?8:20;v=std::clamp(v+lift*v*(255-v)/16384+contrast*v*(255-v)*(2*v-255)/4194304,0,255);}rgb[k]=v;}
        const int gray=(77*rgb[0]+150*rgb[1]+29*rgb[2]+128)/256,saturation=style==1?256:style==2?264:276;
        for(unsigned k=0;k<3;++k)out[k]=static_cast<unsigned char>(std::clamp(gray+(rgb[k]-gray)*saturation/256,0,255));
    }});
}
}
