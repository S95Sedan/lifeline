#include "game/effects/effects.hpp"
#include "game/transform_motion.hpp"
#include <numbers>

namespace lifeline::script {
int Effects::create_title_environment(){
    // 1a9ea8: 8x8 shared vertex intensities; 1a9fe0 consumes their 5x5 corner.
    if(count()==slots_.size())return -1;
    resources_.load(11002);
    Instance effect;effect.type=11002;
    for(unsigned i=0;i<64;++i){Particle point;const float c=.5f+float(random_()&4095)/4095.f;point.color={c,c,c,1};effect.particles.push_back(point);}
    effect.textures[0]=texture(11002,"logoBG_A.raw");effect.textures[1]=texture(11002,"logoBG_B.raw");effect.textures[2]=texture(11002,"sky01.raw");
    return insert(std::move(effect));
}
std::vector<ModelMesh> Effects::title_environment_geometry(const Instance& effect,const SceneCamera& camera,std::array<unsigned,3> textures,unsigned width,unsigned height)const{
    std::vector<ModelMesh> result;
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));
    const auto right=math::normalize_xyz(math::cross_xyz(forward,camera.up))*(camera.mirror_horizontal?-1.f:1.f),up=math::cross_xyz(math::normalize_xyz(math::cross_xyz(forward,camera.up)),forward);
    const float depth=std::max(1.f,camera.near_plane*2),scale=depth/camera.focal_pixels;
    const auto point=[&](float x,float y){return camera.eye+forward*depth+right*(x*scale)+up*(-y*scale);};
    ModelMesh background;background.flags=0x44000000;background.texture=textures[effect.parameters[0]!=0?1:0];background.depth_test=background.depth_write=false;background.fog_mode=0;
    background.effect_blend=std::array<unsigned,4>{0,1,0,1};
    // The stars use destination alpha. Their cube already extends past the
    // original viewport, but the alpha backing used to stop at its edges.
    // Preserve the center mapping and extend only its edge mask into the new
    // margins; the stars themselves retain their original geometry and UVs.
    const float display_scale=std::min(float(width)/640,float(height)/448);
    const float half_x=width/(2*display_scale),half_y=height/(2*display_scale);
    const float xs[]{-half_x,-320,320,half_x},ys[]{-half_y,-224,224,half_y};
    const float us[]{0,0,.625f,.625f},vs[]{0,0,.875f,.875f};
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){
        if(xs[x]==xs[x+1]||ys[y]==ys[y+1])continue;
        // The populated mask ends at 320x224 inside a padded texture.
        // Sample the last populated texel in extended margins, not padding
        // immediately beyond it (which darkened the right-hand starfield).
        const auto& mask=*effect.textures[effect.parameters[0]!=0?1:0];
        const float u0=x==0?.5f/mask.width:x==2?.625f-.5f/mask.width:us[x];
        const float u1=x==1?us[x+1]:u0;
        const float v0=y==0?.5f/mask.height:y==2?.875f-.5f/mask.height:vs[y];
        const float v1=y==1?vs[y+1]:v0;
        const auto base=unsigned(background.vertices.size());
        background.vertices.insert(background.vertices.end(),{{point(xs[x],ys[y]),{},{1,1,1,1},u0,v0},{point(xs[x+1],ys[y]),{},{1,1,1,1},u1,v0},{point(xs[x],ys[y+1]),{},{1,1,1,1},u0,v1},{point(xs[x+1],ys[y+1]),{},{1,1,1,1},u1,v1}});
        background.indices.insert(background.indices.end(),{base,base+1,base+2,base+1,base+2,base+3});
    }
    result.push_back(std::move(background));
    // Six normalized cube faces, four strips per face, repeated sky UVs.
    TransformMotion turn;turn.rotation.y=effect.age;const auto rotation=turn.matrix();
    // 1aa010 / 18d8d8: camera eye (controller +160), a position vector.
    const auto center=camera.eye;
    for(unsigned face=0;face<6;++face){
        const auto orient=[&](math::Vector4 v){switch(face){case 1:return math::Vector4{v.z,v.y,-v.x,0};case 2:return math::Vector4{-v.x,v.y,-v.z,0};case 3:return math::Vector4{-v.z,v.y,v.x,0};case 4:return math::Vector4{v.x,-v.z,v.y,0};case 5:return math::Vector4{v.x,v.z,-v.y,0};default:return v;}};
        for(unsigned row=0;row<4;++row){ModelMesh mesh;mesh.texture=textures[2];mesh.flags=0x44000000;mesh.depth_test=mesh.depth_write=false;mesh.repeat_u=mesh.repeat_v=true;mesh.fog_mode=0;mesh.effect_blend=std::array<unsigned,4>{0,1,1,1};
            for(unsigned col=0;col<=4;++col)for(unsigned edge=0;edge<2;++edge){const float u=col/4.f,v=(row+edge)/4.f;
                const auto local=orient(math::normalize_xyz({2*u-1,2*v-1,1,0})*100);
                mesh.vertices.push_back({center+math::transform(rotation,local),{},effect.particles[(row+edge)*8+col].color,u*3,v*3});
            }
            for(unsigned i=2;i<mesh.vertices.size();++i)mesh.indices.insert(mesh.indices.end(),{i-2,i-1,i});result.push_back(std::move(mesh));
        }
    }
    return result;
}
}
