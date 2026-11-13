#include "game/effects/effects.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lifeline::script {
namespace {
int integer_selector(float value){if(double(value)<-2147483648.||double(value)>=2147483648.)throw std::runtime_error("Invalid glow attachment");return int(std::nearbyint(value));}
float fade(float age,float lifetime){return lifetime>0?std::max(0.f,1-age/lifetime):0.f;}
}
StaticModel Effects::geometry(const SceneCamera& camera,bool include_title_environment,unsigned viewport_width,unsigned viewport_height)const{
    StaticModel model;model.nodes=1;model.textures.push_back({"effect white",1,1,Bytes{255,255,255,255}});
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1));
    const auto right=math::normalize_xyz(math::cross_xyz(camera.up,forward));
    const auto up=math::cross_xyz(forward,right);
    std::map<const AssetImage*,unsigned> textures;
    std::optional<unsigned> breakable_texture;
    const auto texture_index=[&](const std::shared_ptr<const AssetImage>& image){
        if(!image)return 0u;
        if(auto it=textures.find(image.get());it!=textures.end())return it->second;
        const auto index=unsigned(model.textures.size());model.textures.push_back(*image);textures.emplace(image.get(),index);return index;
    };
    const auto mesh=[&](unsigned texture){ModelMesh result;result.texture=texture;result.flags=0x48000000;return result;};
    const auto strip=[&](ModelMesh& mesh){for(unsigned i=2;i<mesh.vertices.size();++i){mesh.indices.push_back(i-2);mesh.indices.push_back(i-1);mesh.indices.push_back(i);}model.meshes.push_back(std::move(mesh));};
    for(const auto& slot:slots_)if(slot&&!slot->loading){
        const auto& gun=*slot;const auto& p=gun.parameters;
        if(gun.type==10001){
            // The original samples its dynamically captured `mirror.raw`.
            // Use a neutral glass fallback instead of the white utility texel
            // while that render target is not represented by the native path.
            if(!breakable_texture){breakable_texture=unsigned(model.textures.size());model.textures.push_back({"breakable glass",1,1,Bytes{112,124,136,255}});}
            const math::Vector4 color{p[9],p[10],p[11],p[12]};
            const auto rotate=[](math::Vector4 point,math::Vector4 angles){
                const float cx=std::cos(angles.x),sx=std::sin(angles.x),cy=std::cos(angles.y),sy=std::sin(angles.y),cz=std::cos(angles.z),sz=std::sin(angles.z);
                point={point.x,point.y*cx-point.z*sx,point.y*sx+point.z*cx,0};
                point={point.x*cy+point.z*sy,point.y,-point.x*sy+point.z*cy,0};
                return math::Vector4{point.x*cz-point.y*sz,point.x*sz+point.y*cz,point.z,0};
            };
            for(const auto& shard:gun.shards){
                auto triangle=mesh(*breakable_texture);triangle.flags=0x40000000;
                for(const auto local:shard.local)triangle.vertices.push_back({shard.center+rotate(local,shard.rotation),{},color,0,0});
                triangle.indices={0,1,2};model.meshes.push_back(std::move(triangle));
            }
            continue;
        }
        if(gun.type==5000)continue;
        if(gun.type==2002||gun.type==-68||gun.type==-69||gun.type==-70){auto meshes=heron_geometry(gun,camera,{texture_index(gun.textures[0]),texture_index(gun.textures[1]),texture_index(gun.textures[2])});for(auto& m:meshes)model.meshes.push_back(std::move(m));continue;}
        if(gun.type==6000||gun.type==6001){auto meshes=plant_geometry(gun,camera,{texture_index(gun.textures[0]),texture_index(gun.textures[1]),0});for(auto& m:meshes)model.meshes.push_back(std::move(m));continue;}
        if(gun.type==11002){if(!include_title_environment)continue;auto meshes=title_environment_geometry(gun,camera,{texture_index(gun.textures[0]),texture_index(gun.textures[1]),texture_index(gun.textures[2])},viewport_width,viewport_height);for(auto& mesh:meshes)model.meshes.push_back(std::move(mesh));continue;}
        if(gun.type==10002){auto meshes=energy_projectile_geometry(gun);for(auto& mesh:meshes)model.meshes.push_back(std::move(mesh));continue;}
        if(gun.type==5001||gun.type==10011)continue;
        if(gun.type==-66){model.meshes.push_back(lightning_ribbon_geometry(gun,texture_index(gun.textures[0])));continue;}
        if(gun.type==10012){
            for(unsigned i=0;i<40;++i){const auto& particle=gun.particles[i];if(particle.age>=particle.life)continue;
                auto h=right*particle.size,v=up*particle.size;
                if(i>=32){
                    const auto axis=particle.direction;const float a=float(int(std::nearbyint(particle.angle*.15915493667125702f*65536.f))&65535)*(2*std::numbers::pi_v<float>/65536.f);
                    const auto rotate=[&](math::Vector4 value){return value*std::cos(a)+math::cross_xyz(axis,value)*std::sin(a)+axis*(math::dot_xyz(axis,value)*(1-std::cos(a)));};
                    h=rotate({particle.size,0,0,0});v=rotate({0,0,particle.size,0});
                }
                const math::Vector4 color=i<32?math::Vector4{.5f,.7f,1,particle.growth/particle.initial_growth}:math::Vector4{.8f,1,.9f,particle.growth/particle.initial_growth};
                auto draw=mesh(texture_index(gun.textures[i==32?1:0]));const auto center=particle.position;
                draw.vertices={{center+h*(-1)+v*(-1),{},color,0,0},{center+h+v*(-1),{},color,1,0},{center+h*(-1)+v,{},color,0,1},{center+h+v,{},color,1,1}};strip(draw);
            }
            continue;
        }
        if(gun.type==10005){auto shapes=energy_trail_geometry(gun,camera,texture_index(gun.textures[0]));model.meshes.insert(model.meshes.end(),std::make_move_iterator(shapes.begin()),std::make_move_iterator(shapes.end()));continue;}
        if(gun.type==10004){
            if(!gun.textures[0])continue; // 16fef0 skips drawing if upload failed.
            for(const auto& bubble:gun.particles)if(bubble.growth!=0){
                const float angle=float(int(std::nearbyint(bubble.angle*65536.f))&65535)*(2*std::numbers::pi_v<float>/65536.f);
                const auto center=bubble.position+right*(std::cos(angle)*bubble.size),h=right*bubble.size,v=up*bubble.size;
                const math::Vector4 color{p[6],p[7],p[8],p[9]};auto draw=mesh(texture_index(gun.textures[0]));
                draw.vertices={{center+h+v,{},color,0,0},{center+h*(-1)+v,{},color,1,0},{center+h*(-1)+v*(-1),{},color,1,1},{center+h+v*(-1),{},color,0,1}};
                draw.indices={0,1,2,0,2,3};model.meshes.push_back(std::move(draw));
            }
            continue;
        }
        if(gun.type==12){
            const auto center=math::transform(attachment_(integer_selector(p[1]),integer_selector(p[2])),{p[3],p[4],p[5],1});
            for(const auto& particle:gun.particles){
                const auto axis=particle.direction;const float a=float(int(std::nearbyint(particle.angle*.15915493667125702f*65536.f))&65535)*(2*std::numbers::pi_v<float>/65536.f);
                const auto rotate=[&](math::Vector4 v){return v*std::cos(a)+math::cross_xyz(axis,v)*std::sin(a)+axis*(math::dot_xyz(axis,v)*(1-std::cos(a)));};
                const auto h=rotate({particle.size,0,0,0}),v=rotate({0,particle.size,0,0});
                const math::Vector4 color{p[7],p[8],p[9],p[10]};auto draw=mesh(texture_index(gun.textures[0]));
                draw.depth_test=draw.depth_write=false;
                draw.vertices={{center+h*(-1)+v,{},color,0,0},{center+h*(-1)+v*(-1),{},color,0,1},
                    {center+h+v,{},color,1,0},{center+h+v*(-1),{},color,1,1}};
                strip(draw);
            }
            continue;
        }
        if(gun.type==16){
            // 17d828 draws at +Z, -Z, +X, -X after the two fixed
            // rotations and the changing Y phase; each sprite faces the eye.
            const auto turn=[](float a){return float(int(a*.15915493667125702f*65536.f)&65535)*(2*std::numbers::pi_v<float>/65536.f);};
            for(const auto& particle:gun.particles)if(particle.color.w>0){
                const float x=turn(particle.rotation.x),y=turn(particle.rotation.y)+turn(particle.angle);
                for(const auto local:std::array<math::Vector4,4>{{{0,0,1,0},{0,0,-1,0},{1,0,0,0},{-1,0,0,0}}}){
                    const auto q=math::Vector4{local.x*std::cos(y)+local.z*std::sin(y),0,-local.x*std::sin(y)+local.z*std::cos(y),0};
                    const auto center=gun.origin+math::Vector4{q.x,-q.z*std::sin(x),q.z*std::cos(x),0}*particle.extent;
                    auto draw=mesh(texture_index(gun.textures[0]));const auto h=right*particle.size,v=up*particle.size;
                    draw.vertices={{center+h*(-1)+v*(-1),{},particle.color,0,0},{center+h+v*(-1),{},particle.color,1,0},
                        {center+h+v,{},particle.color,1,1},{center+h*(-1)+v,{},particle.color,0,1}};
                    draw.indices={0,1,2,0,2,3};model.meshes.push_back(std::move(draw));
                }
            }
            continue;
        }
        if(gun.type==2){model.meshes.push_back(debris_geometry(gun));continue;}
        if(gun.type==10006){auto meshes=wave_geometry(gun,camera);
            model.meshes.insert(model.meshes.end(),std::make_move_iterator(meshes.begin()),std::make_move_iterator(meshes.end()));continue;}
        if(gun.type==7000){auto meshes=shell_geometry(gun,{texture_index(gun.textures[0]),texture_index(gun.textures[1]),texture_index(gun.textures[2])});
            model.meshes.insert(model.meshes.end(),std::make_move_iterator(meshes.begin()),std::make_move_iterator(meshes.end()));continue;}
        if(gun.explosion_child!=Instance::ExplosionChild::none){
            const auto tex=texture_index(gun.textures[0]);model.meshes.push_back(explosion_geometry(gun,camera,tex));
            if(gun.explosion_child==Instance::ExplosionChild::star)model.meshes.push_back(explosion_geometry(gun,camera,tex,-1));
            continue;
        }
        if(gun.type==9||gun.type==10003)continue;
        if(gun.type==5){
            // 1a9b50: six normalized cube patches, four strips per patch.
            const float angle=float(int(gun.age*.15915493667125702f*65536.f)&65535)*(2*std::numbers::pi_v<float>/65536.f);
            for(unsigned face=0;face<6;++face)for(unsigned row=0;row<4;++row){
                auto patch=mesh(texture_index(gun.textures[0]));patch.flags=0x44000000;
                patch.background=true;patch.depth_write=false;patch.repeat_u=patch.repeat_v=patch.linear_filter=true;
                for(unsigned col=0;col<=4;++col)for(unsigned next=0;next<2;++next){
                    const float u=float(col)*.25f,v=float(row+next)*.25f;
                    auto point=math::normalize_xyz({u*2-1,v*2-1,1,0})*100;
                    const auto q=point;
                    switch(face){
                    case 1:point={q.z,q.y,-q.x,0};break;
                    case 2:point={-q.x,q.y,-q.z,0};break;
                    case 3:point={-q.z,q.y,q.x,0};break;
                    case 4:point={q.x,-q.z,q.y,0};break;
                    case 5:point={q.x,q.z,-q.y,0};break;
                    }
                    point={point.x*std::cos(angle)-point.y*std::sin(angle),point.x*std::sin(angle)+point.y*std::cos(angle),point.z,0};
                    patch.vertices.push_back({camera.eye+point,{},gun.particles[(row+next)*8+col].color,u*3,v*3});
                }
                strip(patch);
            }
            continue;
        }
        if(gun.type==10008)continue; // Emitter has no render callback.
        if(gun.type==10009){
            // 170d88: six latitude bands, two counter-scrolling layers,
            // radius 100 around the eye. ZTE and fog are disabled.
            for(int band=-3;band<3;++band)for(float sign:{1.f,-1.f}){
                auto smoke=mesh(texture_index(gun.textures[0]));
                smoke.flags=0x44000000;smoke.depth_test=smoke.depth_write=false;
                smoke.fog_mode=0;smoke.repeat_u=smoke.repeat_v=smoke.linear_filter=true;
                for(int longitude=0;longitude<=16;++longitude)for(int edge=0;edge<2;++edge){
                    const int latitude=band+edge;
                    const float a=float(latitude)*std::numbers::pi_v<float>/8;
                    const float b=float(longitude)*std::numbers::pi_v<float>/8;
                    const auto point=math::Vector4{100*std::cos(a)*std::cos(b),100*std::sin(a),100*std::cos(a)*std::sin(b),0};
                    const math::Vector4 color{1,1,1,(latitude==-3||latitude==3)?0.f:.03f};
                    smoke.vertices.push_back({camera.eye+point,{},color,float(longitude)*.5f+sign*p[0],float(latitude)*.5f-p[1]});
                }
                strip(smoke);
            }
            continue;
        }
        if(gun.type==10000){
            // 198f80..198fdc uses world up, independent of camera roll.
            const auto h=math::normalize_xyz(math::cross_xyz(forward,{0,1,0,0}));
            const auto v=math::normalize_xyz(math::cross_xyz(h,forward));
            const auto color=gun.radial_axis;auto edge=color;edge.w=0;
            for(float sign:{1.f,-1.f}){
                // The original composes two separately quantized rotations.
                const auto quantize=[](float a){return float(integer_selector(a*.15915493667125702f*65536.f)&65535)*(2*std::numbers::pi_v<float>/65536.f);};
                const float angle=quantize(sign*gun.age/gun.lifetime*1.57079649f)+quantize(p[39]);
                const auto rotate=[&](math::Vector4 point){return point*std::cos(angle)+math::cross_xyz(forward,point)*std::sin(angle)+forward*(math::dot_xyz(forward,point)*(1-std::cos(angle)));};
                auto fan=mesh(texture_index(gun.textures[0]));fan.flags=p[12]==1?0x48000000:0x44000000;
                fan.vertices.push_back({gun.origin,{},color,.5f,.5f});
                for(unsigned i=0;i<=8;++i){const float a=float(i)*std::numbers::pi_v<float>/4,x=std::cos(a),y=std::sin(a);
                    fan.vertices.push_back({gun.origin+rotate(h*(-x*p[38])+v*(-y*p[38])),{},edge,.5f+y*.5f,.5f+x*.5f});
                    if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
                model.meshes.push_back(std::move(fan));
            }
            continue;
        }
        if(gun.type==10010){
            const auto attachment=attachment_(int(p[1]),int(p[2]));
            const auto origin=math::transform(attachment,{p[3],p[4],p[5],1});
            for(const auto& ray:gun.particles)if(ray.age>=0){
                const auto axis=math::transform(attachment,ray.direction);
                auto y=math::normalize_xyz(math::cross_xyz(forward,axis));
                if(math::dot_xyz(y,y)==0)y=math::normalize_xyz(math::cross_xyz(right,axis));
                const auto x=math::cross_xyz(axis,y);const float width=ray.extent*std::min(1.f,ray.age*.25f);
                math::Vector4 color{p[7],p[8],p[9],ray.color.w},edge=color;edge.w=0;
                for(float sign:{1.f,-1.f}){
                    auto beam=mesh(texture_index(gun.textures[0]));beam.repeat_u=beam.repeat_v=true;
                    beam.vertices.push_back({origin,{},color,.5f,ray.angle});
                    for(unsigned i=1;i<=4;++i){const float t=float(i)*.25f;auto center_color=color;center_color.w*=1-t;
                        const auto center=origin+axis*(t*ray.size);
                        beam.vertices.push_back({center+y*(sign*width*t),{},edge,.5f+sign*.5f*t,ray.angle+t});
                        beam.vertices.push_back({center+x*(-width*t),{},center_color,.5f,ray.angle+t});
                    }
                    strip(beam);
                }
            }
            continue;
        }
        if(gun.type==14){
            for(const auto& smoke:gun.particles)if(gun.age<smoke.life){
                const float t=gun.age/smoke.life,opacity=(1-t)/(1+4*t),size=(1-opacity)*smoke.size;
                auto color=smoke.color;color.w*=opacity;auto edge=color;edge.w=0;
                for(float sign:{1.f,-1.f}){
                    const float radians=smoke.angle*sign;
                    const float angle=float(integer_selector(radians*(65536.f/(2*std::numbers::pi_v<float>)))&65535)*(2*std::numbers::pi_v<float>/65536);
                    const auto h=(right*std::cos(angle)+up*std::sin(angle))*size;
                    const auto v=(up*std::cos(angle)+right*(-std::sin(angle)))*size;
                    auto fan=mesh(texture_index(gun.textures[0]));fan.flags=0x44000000;
                    fan.vertices.push_back({smoke.position,{},color,.5f,.5f});
                    for(unsigned i=0;i<=8;++i){const float a=float(i)*std::numbers::pi_v<float>/4,x=std::cos(a),y=std::sin(a);
                        fan.vertices.push_back({smoke.position+h*x+v*y,{},edge,.5f+y*.5f,.5f+x*.5f});
                        if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
                    model.meshes.push_back(std::move(fan));
                }
            }
            continue;
        }
        if(gun.type==8){
            if(!gun.textures[0])continue; // 1ab790 rejects a missing static texture.
            const auto start=math::transform(attachment_(int(p[1]),int(p[2])),{p[3],p[4],p[5],1});
            const auto end=math::transform(attachment_(int(p[6]),int(p[7])),{p[8],p[9],p[10],1});
            const auto side=math::normalize_xyz(math::cross_xyz(math::normalize_xyz(end+camera.eye*(-1)),math::normalize_xyz(start+camera.eye*(-1))));
            if(math::dot_xyz(side,side)==0)continue;
            const math::Vector4 color{p[11],p[11],p[11],p[14]*(1-gun.age/p[17])};
            auto ribbon=mesh(texture_index(gun.textures[0]));
            for(unsigned i=0;i<16;++i){
                const float t=float(i)/15;const auto center=start*t+end*(1-t)+gun.particles[i].position;
                ribbon.vertices.push_back({center+side*(p[16]*.5f),{},color,p[19],t});
                ribbon.vertices.push_back({center+side*(-p[16]*.5f),{},color,p[19]+.25f,t});
            }
            strip(ribbon);continue;
        }
        if(gun.type==17){
            for(const auto& particle:gun.particles)if(gun.age<=particle.life){
                const float t=gun.age/particle.life,opacity=(1-t)/(1+4*t),size=(1-opacity)*particle.size;
                const auto h=right*size,v=up*size,center=particle.position;
                const math::Vector4 color{p[15],p[16],p[17],p[18]*opacity};
                auto quad=mesh(texture_index(gun.textures[0]));
                quad.vertices={{center+h*(-1)+v*(-1),{},color,0,0},{center+h+v*(-1),{},color,1,0},
                               {center+h*(-1)+v,{},color,0,1},{center+h+v,{},color,1,1}};
                strip(quad);
            }
            continue;
        }
        if(gun.type==3){
            const int mode=int(p[14]);
            for(const auto& spark:gun.particles)if(spark.age<=spark.life){
                const auto side=math::normalize_xyz(math::cross_xyz(forward,spark.direction))*(p[17]*.5f);
                const auto tail=spark.position+spark.direction*(-p[18]);
                math::Vector4 color{p[10],p[11],p[12],mode==1||mode==2?p[13]*(1-spark.age/spark.life):1};
                auto edge=color;if(mode==1||mode==2)edge.w=0;
                auto trail=mesh(0);trail.flags=mode==2?0x48000000:mode==1?0x44000000:0x40000000;
                trail.vertices={{spark.position+side,{},color,0,0},{spark.position+side*(-1),{},color,0,0},
                                {tail+side,{},edge,0,0},{tail+side*(-1),{},edge,0,0}};
                strip(trail);
            }
            continue;
        }
        if(gun.type==11){
            auto flash=*gun.authored_mesh;const float t=gun.age/gun.lifetime;
            for(auto& v:flash.vertices)v.color.w*=(1-t)/(1+4*t);
            model.meshes.push_back(std::move(flash));continue;
        }
        if(gun.type==7){
            const auto end=gun.origin+gun.direction*p[21];
            const auto side=math::normalize_xyz(math::cross_xyz(camera.eye+gun.origin*(-1),end+gun.origin*(-1)));
            auto color=gun.radial_axis;color.w*=1-gun.age/gun.lifetime;
            auto edge=color;edge.w=0;
            for(float sign:{1.f,-1.f}){
                auto ribbon=mesh(texture_index(gun.textures[0]));const float u=sign>0?1.f:0.f;
                ribbon.vertices={{gun.origin+side*(sign*p[20]),{},edge,u,0},{gun.origin,{},color,.5f,0},
                    {end+side*(sign*p[20]),{},edge,u,1},{end,{},color,.5f,1}};
                strip(ribbon);
            }
            continue;
        }
        if(gun.type==15||gun.type==13){
            const float t=gun.age/gun.lifetime;
            const float opacity=(1-t)/(1+4*t),radius=(1-opacity)*p[9];
            auto color=gun.radial_axis;color.w*=opacity;
            auto ring=mesh(texture_index(gun.textures[0]));
            const auto x=gun.trail[0]*radius,z=gun.trail[1]*radius;
            ring.vertices={{gun.origin+x*(-1)+z*(-1),{},color,0,0},
                {gun.origin+x*(-1)+z,{},color,0,1},{gun.origin+x+z*(-1),{},color,1,0},
                {gun.origin+x+z,{},color,1,1}};
            strip(ring);continue;
        }
        if(gun.type==3003||gun.type==3004||gun.type==-64)continue;
        if(gun.type==3000){
            // 172340 uses the camera's forward axis and world Y, independent
            // of camera roll. Both layers have transparent perimeter vertices.
            const auto side=math::normalize_xyz(math::cross_xyz(forward,{0,1,0,0}));
            const auto vertical=math::normalize_xyz(math::cross_xyz(side,forward));
            const float ratio=gun.age/gun.lifetime;
            for(unsigned layer=0;layer<2;++layer){
                const float radius=(layer?2.5f:5.f)*(1+ratio);
                const float radians=p[22]+(layer?-1.f:1.f)*ratio*1.57079649f;
                const float angle=float(integer_selector(radians*(65536.f/(2*std::numbers::pi_v<float>)))&65535)*(2*std::numbers::pi_v<float>/65536);
                const auto horizontal=(side*std::cos(angle)+math::cross_xyz(forward,side)*std::sin(angle))*radius;
                const auto height=(vertical*std::cos(angle)+math::cross_xyz(forward,vertical)*std::sin(angle))*radius;
                auto fan=mesh(texture_index(gun.textures[0]));fan.flags=0x44000000;
                fan.vertices.push_back({gun.origin,{},{.5f,.5f,.5f,1},.5f,.5f});
                for(unsigned i=0;i<=8;++i){const float a=i*std::numbers::pi_v<float>/4;
                    const float x=std::cos(a),y=std::sin(a);
                    fan.vertices.push_back({gun.origin+horizontal*(-x)+height*(-y),{},{.5f,.5f,.5f,0},.5f+y*.5f,.5f+x*.5f});
                    if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
                model.meshes.push_back(std::move(fan));
            }
            continue;
        }
        if(gun.type==-62){
            auto fan=mesh(0);auto color=gun.radial_axis;color.w*=fade(gun.age,gun.lifetime);auto edge=color;edge.w=0;
            fan.vertices.push_back({gun.origin,{},color,0,0});
            for(unsigned i=0;i<=8;++i){const float angle=i*std::numbers::pi_v<float>/4;
                fan.vertices.push_back({gun.origin+right*(std::cos(angle)*p[0]*fade(gun.age,gun.lifetime))+up*(std::sin(angle)*p[0]*fade(gun.age,gun.lifetime)),{},edge,0,0});
                if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
            model.meshes.push_back(std::move(fan));continue;
        }
        if(gun.type==-63){
            const auto a=gun.trail[0],b=gun.trail[1],c=gun.trail[2];
            const auto side_a=math::normalize_xyz(math::cross_xyz(camera.eye+a*(-1),b+a*(-1)));
            const auto side_b=math::normalize_xyz(math::cross_xyz(camera.eye+b*(-1),c+b*(-1)));
            auto color=gun.radial_axis;color.w*=fade(gun.age,gun.lifetime);auto edge=color;edge.w=0;
            for(float sign:{1.f,-1.f}){
                auto ribbon=mesh(0);ribbon.vertices={{a+side_a*(p[0]*sign),{},edge,0,0},{a,{},color,0,0},{b+side_b*(p[1]*sign),{},edge,0,0},{b,{},color,0,0}};
                strip(ribbon);
            }
            continue;
        }
        if(gun.authored_mesh){auto draw=*gun.authored_mesh;draw.texture=texture_index(gun.textures[0]);for(auto& vertex:draw.vertices)vertex.color.w=fade(gun.age,gun.lifetime);model.meshes.push_back(std::move(draw));continue;}
        if(gun.type==4003)continue; // The emitter itself has no geometry.
        if(gun.type==0||gun.type==4||gun.type==-65||gun.type==-67){
            // 1a5950 / 1a6910: two counter-rotating, eight-segment smoke fans.
            const auto color=gun.radial_axis;auto edge=color;edge.w=0;
            for(float sign:{1.f,-1.f}){
                const float radians=p[39]+sign*gun.age/gun.lifetime*1.57079649f;
                const float angle=float(integer_selector(radians*(65536.f/(2*std::numbers::pi_v<float>)))&65535)*(2*std::numbers::pi_v<float>/65536);
                const auto horizontal=(right*std::cos(angle)+up*std::sin(angle))*p[38];
                const auto vertical=(up*std::cos(angle)+right*(-std::sin(angle)))*p[38];
                auto fan=mesh(texture_index(gun.textures[0]));fan.flags=(gun.type==-67||((gun.type==4||gun.type==-65)&&p[12]==1))?0x48000000:0x44000000;
                fan.vertices.push_back({gun.origin,{},color,.5f,.5f});
                for(unsigned i=0;i<=8;++i){const float a=i*std::numbers::pi_v<float>/4;
                    const float x=std::cos(a),y=std::sin(a);
                    fan.vertices.push_back({gun.origin+horizontal*x+vertical*y,{},edge,.5f+y*.5f,.5f+x*.5f});
                    if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
                model.meshes.push_back(std::move(fan));
            }
            continue;
        }
        if(gun.type==6){
            const auto matrix=attachment_(integer_selector(p[1]),integer_selector(p[2]));
            const auto origin=math::transform(matrix,{p[3],p[4],p[5],1});
            const auto direction=math::normalize_xyz(math::transform(matrix,{p[8],p[9],p[10],0}));
            const float facing=p[0]==1?std::max(0.f,math::dot_xyz(math::normalize_xyz(camera.eye+origin*(-1)),direction)):1;
            const float size=p[15]*facing;if(size<=0)continue;
            const auto center=origin+math::normalize_xyz(math::Vector4{p[3],p[4],p[5],1}+camera.eye*(-1))*p[16];
            const math::Vector4 color{p[11],p[12],p[13],p[14]*(gun.age==0?.85f:1)};
            auto glow=mesh(texture_index(gun.textures[0]));
            glow.vertices={{center+right*(-size)+up*size,{},color,0,0},{center+right*size+up*size,{},color,0,1},
                {center+right*size+up*(-size),{},color,1,1},{center+right*(-size)+up*(-size),{},color,1,0}};
            glow.indices={0,1,2,0,2,3};model.meshes.push_back(std::move(glow));continue;
        }
        if(gun.type==10007){
            std::array<math::Vector4,3> points;
            for(unsigned i=0;i<3;++i)points[i]=math::transform(attachment_(int(p[1]),int(p[2+i*4])),{p[3+i*4],p[4+i*4],p[5+i*4],1});
            const float age=std::max(0.f,gun.age-1);
            const float alpha=age<p[21]?(p[21]>0?age/p[21]:1):age<p[23]-p[22]?1:(p[22]>0?1-(age-(p[23]-p[22]))/p[22]:0);
            // 185e40..185fc8: eight fans along each of two attached segments.
            for(unsigned segment=0;segment<2;++segment)for(unsigned step=0;step<8;++step){
                const float phase=float((gun.aura_angle+(segment*8+step)*4096)&65535)*(2*std::numbers::pi_v<float>/65536);
                const float size=p[14]+(p[15]-p[14])*(std::sin(phase)+1)*.5f,t=step/8.f;
                const auto center=points[segment]*(1-t)+points[segment+1]*t;
                const math::Vector4 color{p[17],p[18],p[19],p[20]*std::clamp(alpha,0.f,1.f)},edge{p[17],p[18],p[19],0};
                auto fan=mesh(0);fan.vertices.push_back({center,{},color,0,0});
                for(unsigned i=0;i<=8;++i){const float angle=i*std::numbers::pi_v<float>/4;
                    fan.vertices.push_back({center+right*(std::sin(angle)*size)+up*(std::cos(angle)*size),{},edge,0,0});if(i)fan.indices.insert(fan.indices.end(),{0,i,i+1});}
                model.meshes.push_back(std::move(fan));
            }
            continue;
        }
        if(gun.type==1002||gun.type==1005||gun.type==4002||gun.type<0){
            for(const auto& particle:gun.particles){
                if(particle.age>=particle.life)continue;
                auto color=particle.color;
                if(particle.kind==Particle::star||particle.kind==Particle::ring)color.w*=particle.initial_growth?particle.growth/particle.initial_growth:0;
                else if(particle.kind!=Particle::flash)color.w*=fade(particle.age,particle.life);
                float size=particle.size;
                if(particle.kind==Particle::smoke)size+=particle.growth*particle.age/particle.life;
                auto horizontal=right,vertical=up;
                if(particle.kind==Particle::fragment){
                    vertical=particle.direction*(particle.speed*particle.growth);
                    horizontal=math::normalize_xyz(math::cross_xyz(forward,vertical))*size;
                }else{
                    if(particle.kind==Particle::ring){
                        horizontal=math::normalize_xyz(math::cross_xyz(particle.direction,up));
                        if(math::dot_xyz(horizontal,horizontal)==0)horizontal=right;
                        vertical=math::cross_xyz(particle.direction,horizontal);
                    }
                    const auto h=horizontal*std::cos(particle.angle)+vertical*std::sin(particle.angle);
                    vertical=(vertical*std::cos(particle.angle)+horizontal*(-std::sin(particle.angle)))*size;horizontal=h*size;
                }
                auto quad=mesh(texture_index(particle.texture));const auto center=particle.position;
                if(particle.kind==Particle::spray){
                    auto side=math::normalize_xyz(math::cross_xyz(forward,particle.direction));const auto end=center+particle.direction*particle.extent;
                    quad.vertices={{center+side*particle.size,{},color,0,0},{center+side*(-particle.size),{},color,1,0},
                                   {end+side*particle.far_size,{},color,0,1},{end+side*(-particle.far_size),{},color,1,1}};quad.indices={0,1,2,1,3,2};
                }else if(particle.kind==Particle::flash){
                    // 1a6910 uses an eight-segment fan, transparent at its edge.
                    quad.vertices.push_back({center,{},color,.5f,.5f});auto edge=color;edge.w=0;
                    for(unsigned i=0;i<=8;++i){const float angle=i*std::numbers::pi_v<float>/4;const float x=std::sin(angle),y=std::cos(angle);
                        quad.vertices.push_back({center+horizontal*x+vertical*y,{},edge,.5f+x*.5f,.5f+y*.5f});
                        if(i)quad.indices.insert(quad.indices.end(),{0,i,i+1});}
                }else{
                    quad.vertices={{center+horizontal+vertical,{},color,1,0},{center+horizontal+vertical*(-1),{},color,1,1},
                                   {center+horizontal*(-1)+vertical,{},color,0,0},{center+horizontal*(-1)+vertical*(-1),{},color,0,1}};
                    quad.indices={0,1,2,1,3,2};
                }
                model.meshes.push_back(std::move(quad));
            }
            continue;
        }
        auto side=math::normalize_xyz(math::cross_xyz(forward,gun.direction));
        if(math::dot_xyz(side,side)==0)side=math::normalize_xyz(math::cross_xyz(right,gun.direction));
        const auto transverse=math::cross_xyz(gun.direction,side);
        const auto point=[&](float x,float y,float z){return gun.origin+transverse*x+side*y+gun.direction*z;};
        // 1893bc..189464: untextured streak strip with alpha zero at its tip.
        const float streak_alpha=fade(gun.age,p[15])*p[14];
        if(streak_alpha>0 && gun.type!=1007 && gun.type!=1008){
            auto streak=mesh(0);const math::Vector4 near{p[11],p[12],p[13],streak_alpha},far{p[11],p[12],p[13],0};
            streak.vertices={{point(0,p[9]*.5f,0),{},near,0,0},{point(0,-p[9]*.5f,0),{},near,0,0},
                             {point(0,p[9]*.5f,p[10]),{},far,0,0},{point(0,-p[9]*.5f,p[10]),{},far,0,0}};
            strip(streak);
        }
        // 189548..1896c0: six-vertex strip, center UV column splits the flame.
        const float fire_alpha=fade(gun.age,p[22]);
        if(fire_alpha>0){
            auto fire=mesh(texture_index(gun.textures[1]));const math::Vector4 color{1,1,1,fire_alpha};
            // Shotgun and launcher use 18ad90: no streak, flame extends 1/4
            // behind its origin rather than the handgun's 1/10.
            const float behind=gun.type==1007 || gun.type==1008?.25f:.1f;
            for(unsigned column=0;column<3;++column){const float y=p[16]*(.5f-float(column)*.5f),u=float(column)*.5f;
                fire.vertices.push_back({point(0,y,-p[17]*behind),{},color,u,1});
                fire.vertices.push_back({point(0,y,p[17]*(1-behind)),{},color,u,0});
            }
            strip(fire);
        }
        // 1898bc..1899bc: camera-facing eight-segment textured fan.
        const float flash_alpha=fade(gun.age,p[28]);
        if(flash_alpha>0){
            auto flash=mesh(texture_index(gun.textures[0]));const math::Vector4 color{1,1,1,flash_alpha};
            flash.vertices.push_back({gun.origin,{},color,.5f,.5f});
            for(unsigned i=0;i<=8;++i){
                const float angle=float(i)*std::numbers::pi_v<float>/4;const float x=std::sin(angle),y=std::cos(angle);
                flash.vertices.push_back({gun.origin+right*(x*p[23])+up*(y*p[23]),{},color,x*.5f+.5f,y*.5f+.5f});
                if(i)flash.indices.insert(flash.indices.end(),{0,i,i+1});
            }
            model.meshes.push_back(std::move(flash));
        }
        // 18a338: additional machinegun radial strips, randomized each update.
        const float radial_alpha=fade(gun.age,p[37]);
        if(gun.type==1006 && radial_alpha>0){
            const auto across=math::normalize_xyz(math::cross_xyz(gun.direction,gun.radial_axis));
            for(unsigned ray=0;ray<gun.rays;++ray){
                const float displacement=.05f-gun.jitter[ray]*.1f;
                auto direction=math::normalize_xyz(gun.rays==2?across+gun.radial_axis*displacement:gun.radial_axis+across*displacement);
                const float angle=float(ray)/float(gun.rays)*2*std::numbers::pi_v<float>;
                direction=direction*std::cos(angle)+math::cross_xyz(gun.direction,direction)*std::sin(angle)+gun.direction*(math::dot_xyz(gun.direction,direction)*(1-std::cos(angle)));
                auto width=math::normalize_xyz(math::cross_xyz(forward,direction));
                if(math::dot_xyz(width,width)==0)width=math::normalize_xyz(math::cross_xyz(up,direction));
                const auto origin=gun.origin+gun.direction*(-p[30]);
                auto radial=mesh(texture_index(gun.textures[2]));const math::Vector4 color{1,1,1,radial_alpha};
                radial.vertices={{origin+width*(p[31]*.5f),{},color,0,1},{origin+width*(-p[31]*.5f),{},color,1,1},
                                 {origin+width*(p[31]*.5f)+direction*p[32],{},color,0,0},{origin+width*(-p[31]*.5f)+direction*p[32],{},color,1,0}};
                strip(radial);
            }
        }
    }
    for(auto& mesh:model.meshes)mesh.effect_pass=!mesh.background;
    return model;
}
}

namespace lifeline::script {
std::vector<std::shared_ptr<const Effects::Display>> Effects::displays()const{
    std::vector<std::shared_ptr<const Display>> result;
    for(const auto& slot:slots_)if(slot&&slot->loading)result.push_back(slot->loading);
    return result;
}
std::vector<FlashDraw> Effects::Display::compose()const{
    auto draws=player.compose();
    for(auto& draw:draws)for(auto& v:draw.vertices){v.x+=x;v.y+=y;}
    if(type==11001){
        const math::Vector4 color{.02734375f,.796875f,.796875f,.5f};
        FlashDraw bar;const float left=x+32,top=y+79,right=left+120*level;
        bar.vertices={{left,top,0,0,color},{right,top,0,0,color},{right,top+5,0,0,color},{left,top+5,0,0,color}};bar.indices={0,1,2,0,2,3};draws.push_back(std::move(bar));return draws;
    }
    if(total>0){
        const float width=263.f*(completed+std::min(age/20.f,1.f))/total;
        const math::Vector4 color{.27734375f,.87890625f,.796875f,1};
        FlashDraw bar;bar.vertices={{200,203,0,0,color},{200+width,209,0,0,color},{200+width,203,0,0,color},{200,209,0,0,color}};
        bar.indices={0,1,2,0,3,1};draws.push_back(std::move(bar));
    }
    return draws;
}
}
