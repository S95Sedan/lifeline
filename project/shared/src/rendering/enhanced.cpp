#include "rendering/enhanced.hpp"
#include "rendering/quality.hpp"
#include <algorithm>
#include <execution>
#include <numeric>

namespace lifeline {
EnhancedLights enhanced_lights(math::Vector4 forward){
    forward=math::normalize_xyz(forward);const auto view=forward*-1;
    auto right=math::normalize_xyz(math::cross_xyz(forward,{0,1,0,0}));if(math::dot_xyz(right,right)==0)right={1,0,0,0};
    return {math::normalize_xyz(view+right*-.55f+math::Vector4{0,.8f,0,0}),math::normalize_xyz(view+right*.9f+math::Vector4{0,.2f,0,0}),math::normalize_xyz(forward+right*.5f+math::Vector4{0,.6f,0,0}),view};
}
EnhancedView enhanced_view(const SceneCamera& camera,unsigned width,unsigned height){
    const auto forward=math::normalize_xyz(camera.target+camera.eye*-1),camera_right=math::normalize_xyz(math::cross_xyz(forward,camera.up)),up=math::cross_xyz(camera_right,forward),right=camera_right*(camera.mirror_horizontal?-1.f:1.f);
    auto authored_forward=forward;authored_forward.y=-authored_forward.y;auto lights=enhanced_lights(authored_forward);
    const auto to_view=[&](math::Vector4 v){v.y=-v.y;return math::Vector4{math::dot_xyz(v,right),math::dot_xyz(v,up),math::dot_xyz(v,forward),0};};
    return {to_view(lights.key),to_view(lights.fill),camera.focal_pixels*std::min(float(width)/640,float(height)/448)};
}
void apply_scene_enhanced(AssetImage& image,const AssetImage& source,std::span<const float> depth,std::span<const unsigned char> eligible,const SceneCamera& camera){
    const auto count=std::size_t(image.width)*image.height;
    if(!image.width||!image.height||depth.size()!=count||eligible.size()!=count)throw std::runtime_error("Invalid enhanced scene buffers");
    Bytes valid_surface(eligible.begin(),eligible.end());for(std::size_t i=0;i<count;++i)if(!std::isfinite(depth[i])||depth[i]<=0)valid_surface[i]=0;
    apply_scene_visual_style(image,source,valid_surface);
    const int width=int(image.width),height=int(image.height),aw=(width+1)/2,ah=(height+1)/2;const auto view=enhanced_view(camera,image.width,image.height);
    std::vector<float> ambient(std::size_t(aw)*ah,1);std::vector<int> rows(ah);std::iota(rows.begin(),rows.end(),0);
    const auto valid=[&](int x,int y){return x>=0&&y>=0&&x<width&&y<height&&eligible[std::size_t(y)*width+x]&&std::isfinite(depth[std::size_t(y)*width+x])&&depth[std::size_t(y)*width+x]>0;};
    const auto point=[&](int x,int y){const float z=1/depth[std::size_t(y)*width+x];return math::Vector4{(x+.5f-width*.5f)*z/view.focal,(height*.5f-y-.5f)*z/view.focal,z,0};};
    const auto representative=[&](int x,int y){return std::pair{std::min(x*2+1,width-1),std::min(y*2+1,height-1)};};
    std::for_each(std::execution::par,rows.begin(),rows.end(),[&](int ay){for(int ax=0;ax<aw;++ax){
        const auto [x,y]=representative(ax,ay);if(!valid(x,y))continue;const auto p=point(x,y);math::Vector4 dx{},dy{};float best_x=1e30f,best_y=1e30f;
        for(int sign:{-1,1}){if(valid(x+sign,y)){const auto q=point(x+sign,y);const float change=std::abs(q.z-p.z);if(change<best_x){best_x=change;dx=(q+p*-1)*float(sign);}}if(valid(x,y+sign)){const auto q=point(x,y+sign);const float change=std::abs(q.z-p.z);if(change<best_y){best_y=change;dy=(q+p*-1)*float(sign);}}}
        const float discontinuity=std::max(.15f,p.z*.04f);if(best_x>discontinuity||best_y>discontinuity)continue;auto normal=math::cross_xyz(dx,dy);const float length=math::dot_xyz(normal,normal);if(!std::isfinite(length)||length<1e-12f)continue;normal=normal*(1/std::sqrt(length));if(math::dot_xyz(normal,p)>0)normal=normal*-1;
        constexpr float radius=.85f;const float scale=std::min(float(width)/640,float(height)/448),pixels=std::clamp(view.focal*radius/p.z,2*scale,14*scale);float occlusion=0;
        constexpr float offsets[8][2]={{1,0},{-.5f,0},{0,1},{0,-.5f},{.7071f,.7071f},{-.35355f,.35355f},{.7071f,-.7071f},{-.35355f,-.35355f}};
        for(const auto& offset:offsets){const int sx=int(std::floor(x+offset[0]*pixels+.5f)),sy=int(std::floor(y+offset[1]*pixels+.5f));if(!valid(sx,sy))continue;const auto delta=point(sx,sy)+p*-1;const float distance=math::dot_xyz(delta,delta);if(distance<1e-6f||distance>radius*radius)continue;occlusion+=std::max(0.f,(math::dot_xyz(normal,delta)-.035f)/std::sqrt(distance))*(1-distance/(radius*radius));}
        const float light=.82f+.32f*std::max(0.f,math::dot_xyz(normal,view.key))+.12f*std::max(0.f,math::dot_xyz(normal,view.fill));ambient[std::size_t(ay)*aw+ax]=std::clamp(std::max(.62f,1-occlusion*.16f)*light,.65f,1.2f);
    }});
    rows.resize(height);std::iota(rows.begin(),rows.end(),0);
    std::for_each(std::execution::par,rows.begin(),rows.end(),[&](int y){for(int x=0;x<width;++x){
        if(!valid(x,y))continue;const auto at=std::size_t(y)*width+x;const auto alpha=[&](int u,int v){return source.rgba[(std::size_t(std::clamp(v,0,height-1))*width+std::clamp(u,0,width-1))*4+3];};if(std::min({alpha(x,y),alpha(x-1,y),alpha(x+1,y),alpha(x,y-1),alpha(x,y+1)})<254)continue;
        const float z=1/depth[at];const int ix=int(std::floor((x-1)*.5f)),iy=int(std::floor((y-1)*.5f));
        const auto [x0,y0]=representative(std::clamp(ix,0,aw-1),std::clamp(iy,0,ah-1));const auto [x1,y1]=representative(std::clamp(ix+1,0,aw-1),std::clamp(iy+1,0,ah-1));
        const float fx=x1>x0?std::clamp(float(x-x0)/(x1-x0),0.f,1.f):0,fy=y1>y0?std::clamp(float(y-y0)/(y1-y0),0.f,1.f):0;float sum=0,weights=0;
        for(int v=0;v<2;++v)for(int u=0;u<2;++u){const int ax=std::clamp(ix+u,0,aw-1),ay=std::clamp(iy+v,0,ah-1);const auto [sx,sy]=representative(ax,ay);if(!valid(sx,sy))continue;const float dz=std::abs(1/depth[std::size_t(sy)*width+sx]-z);if(dz>std::max(.12f,z*.025f))continue;const float weight=(u?fx:1-fx)*(v?fy:1-fy)/(1+dz/std::max(.1f,z*.02f));sum+=ambient[std::size_t(ay)*aw+ax]*weight;weights+=weight;}
        const float light=weights>1e-6f?sum/weights:1;for(unsigned k=0;k<3;++k)image.rgba[at*4+k]=static_cast<unsigned char>(std::clamp(std::lround(image.rgba[at*4+k]*light),0l,255l));
    }});
}
}
