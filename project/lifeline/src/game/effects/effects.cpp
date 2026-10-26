#include "game/effects/effects.hpp"
#include <algorithm>
#include <bit>
#include <climits>
#include <numbers>

namespace lifeline::script {
namespace {
constexpr float pi=std::numbers::pi_v<float>;
constexpr std::array<float,41> defaults{
    1,2,3,0,0,3,1,0,0,.1f,20,.5f,.25f,.025f,1,20,2,6,1,1,1,1,12,2,1,1,1,1,8,
    3,1,1,4,1,1,1,1,12,0,1,0};
bool gun_type(int type){return type==1000 || type==1006 || type==1007 || type==1008;}
const char* prefix(int type){switch(type){case 1000:return "handgun";case 1006:return "machinegun";case 1007:return "shotgun";default:return "launcher";}}
int integer(float value){
    if(!std::isfinite(value)||double(value)<INT_MIN||double(value)>INT_MAX)throw std::runtime_error("Invalid live effect integer");
    return int(value);
}
math::Vector4 vector(std::span<const float> values,unsigned i,float w=0){return {values[i],values[i+1],values[i+2],w};}
math::Matrix4 rotate_axis(math::Matrix4 axes,int axis,float radians){
        const unsigned angle=unsigned(integer(radians*.15915493667125702f*65536.f))&65535u;
        const float a=float(angle)*(2*std::numbers::pi_v<float>/65536.f),c=std::cos(a),s=std::sin(a);
        auto rotation=math::identity();const auto unit=rotation;
        const unsigned u=(axis+1)%3,v=(axis+2)%3;
        rotation.columns[u]=unit.columns[u]*c+unit.columns[v]*s;
        rotation.columns[v]=unit.columns[v]*c+unit.columns[u]*(-s);
        return math::multiply(axes,rotation);
}
math::Matrix4 rotate_axes(math::Matrix4 axes,std::span<const float> degrees,bool reverse){
    for(int step=0;step<3;++step){const int axis=reverse?2-step:step;
        axes=rotate_axis(axes,axis,degrees[axis]/180.f*3.1415927410125732f);}
    return axes;
}
math::Vector4 rotate(math::Vector4 value,math::Vector4 axis,float angle){
    // The original angle helper quantizes radians to its 16-bit turn table.
    const auto turn=integer(angle*(1/(2*pi))*65536.f);
    angle=float(turn&65535)*(2*pi/65536.f);
    return value*std::cos(angle)+math::cross_xyz(axis,value)*std::sin(angle)+axis*(math::dot_xyz(axis,value)*(1-std::cos(angle)));
}
math::Vector4 euler(math::Vector4 value,float x,float y,float z){
    value=rotate(value,{1,0,0,0},x);value=rotate(value,{0,1,0,0},y);return rotate(value,{0,0,1,0},z);
}
}
std::shared_ptr<const AssetImage> Effects::texture(int type,const char* member){
    // A surviving script emitter may create another particle after a combat
    // composite released the shared static texture owner. Resolve dependencies
    // at consumption as well as explicit preload; initialization is idempotent
    // and only consults currently loaded archives for missing owners. Existing
    // particles retain their immutable bitmap through shared ownership.
    resources_.load(type);
    for(const auto& resource:resources_.textures(type))if(resource->member==member){
        if(!resource->bitmap)throw std::runtime_error("Live gun effect needs an installed bitmap");
        if(resource->pixel_format==19){
            // The indexed bitmap and its independently owned CSM1 palette are
            // installed PC files. Do not cache by indices alone: palettes can change.
            const auto palette=texture(type,std::string(member).starts_with("logoBG_")?"logoBG_A.clut":"sky01.clut");
            if(!palette||palette->width!=256||palette->height!=1)throw std::runtime_error("Indexed sky requires its installed palette");
            const auto& bytes=*resource->bitmap;
            const auto offset=std::uint32_t(bytes[10])|(std::uint32_t(bytes[11])<<8)|(std::uint32_t(bytes[12])<<16)|(std::uint32_t(bytes[13])<<24);
            const auto width=resource->width,height=resource->height,stride=(width+3)&~3u;
            if(std::uint64_t(offset)+std::uint64_t(stride)*height>bytes.size())throw std::runtime_error("Truncated indexed sky pixels");
            auto image=std::make_shared<AssetImage>(AssetImage{member,width,height,Bytes(width*height*4)});
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                const unsigned index=bytes[offset+(height-1-y)*stride+x];
                const unsigned entry=(index&~24u)|((index&8)<<1)|((index&16)>>1),dst=(y*width+x)*4;
                std::copy_n(palette->rgba.begin()+entry*4,4,image->rgba.begin()+dst);
                image->rgba[dst+3]=std::uint8_t(std::min(255u,unsigned(image->rgba[dst+3])*2));
            }
            return image;
        }
        auto& cached=bitmaps_[resource->bitmap];if(auto image=cached.lock())return image;
        const auto& bytes=*resource->bitmap;
        if(bytes.size()<54 || bytes[28]!=32)throw std::runtime_error("Live gun effect bitmap format differs");
        const auto offset=std::uint32_t(bytes[10])|(std::uint32_t(bytes[11])<<8)|(std::uint32_t(bytes[12])<<16)|(std::uint32_t(bytes[13])<<24);
        const auto width=resource->width,height=resource->height;
        if(std::uint64_t(offset)+std::uint64_t(width)*height*4>bytes.size())throw std::runtime_error("Truncated gun effect pixels");
        auto image=std::make_shared<AssetImage>(AssetImage{member,width,height,Bytes(width*height*4)});
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const auto src=offset+((height-1-y)*width+x)*4,dst=(y*width+x)*4;
            image->rgba[dst]=bytes[src+2];image->rgba[dst+1]=bytes[src+1];image->rgba[dst+2]=bytes[src];image->rgba[dst+3]=bytes[src+3];
        }
        cached=image;return image;
    }
    return {};
}
int Effects::create_smoke(int type,std::span<const float> input,std::shared_ptr<const AssetImage> bitmap){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=32)throw std::runtime_error("Smoke effect requires 32 parameters");
        Instance smoke;smoke.type=type;
        const float defaults[]{0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,.6f,.1f,1,5,1,90,45};
        std::copy(std::begin(defaults),std::end(defaults),smoke.parameters.begin());
        if(!input.empty()){
            const int mode=integer(input[0]);
            if(mode==0)std::copy(input.begin(),input.end(),smoke.parameters.begin());
            else if(mode==1)std::copy(input.begin()+1,input.begin()+6,smoke.parameters.begin()+1);
            else if(mode==2&&type==4)throw std::runtime_error("Smoke angular direction mode needs original uninitialized-vector behavior recovered");
        }
        auto& p=smoke.parameters;
        for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Invalid smoke effect parameter");
        // The original divides directly, including zero-duration children
        // emitted by 17c928. Their lifetime check runs before interpolation.
        smoke.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        smoke.direction=math::transform(attachment_(integer(p[6]),integer(p[7])),vector(p,8));
        smoke.lifetime=p[30];smoke.radial_axis=vector(p,13,p[16]);
        for(unsigned i=0;i<4;++i)p[32+i]=(p[17+i]-p[13+i])/p[31];
        p[36]=(p[28]-p[27])/p[31];p[37]=p[25];p[38]=p[27];
        p[39]=float(random_()&255)/255.f*6.28f-3.14f;
        smoke.textures[0]=std::move(bitmap);
        if(type==4&&!smoke.textures[0])throw std::runtime_error("Smoke effect texture is not installed");
        return insert(std::move(smoke));
}
int Effects::create(int type,std::span<const float> input){
    if(type==5000)return create_heron(input);
    if(type==2002)return create_damage_wave(input);
    if(type==6000)return create_plant(input);
    if(type==6001)return create_plant_pillar(input);
    if(type==11002)return create_title_environment();
    if(type==10011){Instance effect;effect.type=type;return insert(std::move(effect));}
    if(type==10002)return create_energy_projectile(input);
    if(type==5001)return create_lightning_emitter(input);
    if(type==10012)return create_star_burst(input);
    if(type==10005)return create_energy_trail(input);
    if(type==10004)return create_bubbles(input);
    if(type==12)return create_orbit_light(input);
    if(type==16)return create_encounter(input);
    if(type==4000)return create_dust(input);
    if(type==4003)return create_omen(input);
    if(type==10009){
        // 170cf0 ignores the payload and initializes two persistent UV phases.
        if(count()==slots_.size())return -1;
        Instance smoke;smoke.type=type;smoke.textures[0]=texture(type,"camerasmoke.raw");
        return insert(std::move(smoke));
    }
    if(type==9)return create_explosion(input);
    if(type==10003)return create_burst(input);
    if(type==7000)return create_shell(input);
    if(type==10006)return create_wave(input);
    if(type==5){
        if(count()==slots_.size())return -1;
        Instance sky;sky.type=5;sky.particles.resize(64);
        for(auto& vertex:sky.particles){const float c=float(random_()&4095)/4095.f*.8f+.2f;vertex.color={c,c,c,1};}
        sky.textures[0]=texture(5,"sky01.raw");return insert(std::move(sky));
    }
    if(type==10010){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=18)throw std::runtime_error("Light rays require 18 parameters");
        Instance rays;rays.type=type;
        const float defaults[]{0,2,16,0,0,0,64,1,1,1,1,.9f,5,15,20,30,120,300};
        std::copy(std::begin(defaults),std::end(defaults),rays.parameters.begin());
        if(!input.empty()&&integer(input[0])==0)std::copy(input.begin(),input.end(),rays.parameters.begin());
        auto& p=rays.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid light ray parameter");
        p[1]=float(integer(p[1]));p[2]=float(integer(p[2]));p[6]=float(std::clamp(integer(p[6]),1,64));rays.lifetime=p[17];
        const auto unit=[&]{return float(random_()&4095)/4095.f;};
        for(int i=0;i<int(p[6]);++i){
            Particle ray;ray.age=-unit()*p[16];ray.size=p[14]+(p[15]-p[14])*unit();
            const float degrees=p[12]+(p[13]-p[12])*unit();
            const auto angle=unsigned(integer(degrees*.5f*.0027777778450399637f*65536.f))&65535u;
            const float radians=float(angle)*(2*std::numbers::pi_v<float>/65536.f);
            ray.extent=ray.size*std::sin(radians)/std::cos(radians);ray.angle=unit();ray.color.w=unit()<.5f?p[10]:p[11];
            const float x=unit()-.5f,y=unit()-.5f,z=unit()-.5f;ray.direction=math::normalize_xyz({x,y,z,0});
            rays.particles.push_back(ray);
        }
        rays.textures[0]=texture(type,"explosionlight.raw");return insert(std::move(rays));
    }
    if(type==14){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=27)throw std::runtime_error("Smoke line requires 27 parameters");
        Instance line;line.type=type;
        const float defaults[]{0,0,0,-20,30,0,0,0,20,30,0,0,0,0,1,0,1.5707963705062866f,2.5f,5,0,.1f,1,1,1,1,0,60};
        std::copy(std::begin(defaults),std::end(defaults),line.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),line.parameters.begin());
        const auto& p=line.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid smoke line parameter");
        if(integer(p[0])!=0)return -1;
        const auto start=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        const auto end=math::transform(attachment_(integer(p[6]),integer(p[7])),vector(p,8,1));
        auto basis=math::identity();basis.columns[1]=math::normalize_xyz(math::transform(attachment_(integer(p[11]),integer(p[12])),vector(p,13)));
        basis.columns[2]=math::normalize_xyz(math::cross_xyz(basis.columns[1],{1,0,0,0}));
        if(math::dot_xyz(basis.columns[2],basis.columns[2])==0)
            basis.columns[2]=math::normalize_xyz(math::cross_xyz(basis.columns[1],{0,1,0,0}));
        basis.columns[0]=math::cross_xyz(basis.columns[2],basis.columns[1]);
        const auto delta=end+start*(-1);
        const int points=std::min(32,1+integer(std::sqrt(math::dot_xyz(delta,delta))/(p[18]*.25f)));
        const auto unit=[&]{return float(random_()&4095)/4095.f;};
        for(int i=0;i<points;++i){
            const float t=float(i)/float(points);Particle smoke;smoke.position=start*(1-t)+end*t;
            auto direction=rotate_axis(basis,1,unit()*6.2831854820251465f);
            direction=rotate_axis(direction,0,unit()*p[16]-p[16]*.5f);
            smoke.direction=direction.columns[1]*(p[19]+(p[20]-p[19])*unit());
            smoke.angle=unit()*6.2831854820251465f;smoke.size=p[17]+(p[18]-p[17])*unit();
            smoke.life=p[26]+(p[18]-p[17])*unit();smoke.color={p[21],p[22],p[23],p[24]*(1-t)+p[25]*t};
            line.particles.push_back(smoke);
        }
        line.textures[0]=texture(14,"smoke4.raw");return insert(std::move(line));
    }
    if(type==8){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=17)throw std::runtime_error("Lightning ribbon requires 17 parameters");
        Instance lightning;lightning.type=type;
        const float defaults[]{0,0,0,0,30,0,0,0,0,0,0,1,1,1,1,3,3};
        std::copy(std::begin(defaults),std::end(defaults),lightning.parameters.begin());
        if(!input.empty()&&integer(input[0])==0)std::copy(input.begin(),input.end(),lightning.parameters.begin());
        auto& p=lightning.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid lightning ribbon parameter");
        for(unsigned i:{1u,2u,6u,7u})p[i]=float(integer(p[i]));
        attachment_(integer(p[1]),integer(p[2]));attachment_(integer(p[6]),integer(p[7]));
        lightning.particles.resize(16);
        for(auto& point:lightning.particles){point.position={};point.direction={};}
        lightning.textures[0]=texture(8,"thunder.raw");return insert(std::move(lightning));
    }
    if(type==17){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=21)throw std::runtime_error("Particle burst requires 21 parameters");
        Instance burst;burst.type=type;
        const float defaults[]{0,0,0,0,10,0,0,0,0,1,0,6.2831854820251465f,.2f,.7f,1,1,1,1,1,60,120};
        std::copy(std::begin(defaults),std::end(defaults),burst.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),burst.parameters.begin());
        const auto& p=burst.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid particle burst parameter");
        if(integer(p[0])!=0)return -1;
        burst.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        auto basis=math::identity();basis.columns[1]=math::normalize_xyz(vector(p,8));
        basis.columns[2]=math::normalize_xyz(math::cross_xyz(basis.columns[1],{1,0,0,0}));
        if(math::dot_xyz(basis.columns[2],basis.columns[2])==0)
            basis.columns[2]=math::normalize_xyz(math::cross_xyz(basis.columns[1],{0,1,0,0}));
        basis.columns[0]=math::cross_xyz(basis.columns[2],basis.columns[1]);
        burst.radial_axis=gravity_?gravity_(burst.origin):math::Vector4{0,-1,0,0};
        const auto unit=[&]{return float(random_()&4095)/4095.f;};
        for(unsigned i=0;i<32;++i){
            auto direction=rotate_axis(basis,1,unit()*6.2831854820251465f);
            direction=rotate_axis(direction,0,(unit()-.5f)*p[11]);
            Particle particle;particle.position=burst.origin;
            particle.direction=direction.columns[1]*(p[12]+(p[13]-p[12])*unit());
            particle.size=p[14];particle.life=p[19]+(p[20]-p[19])*unit();
            burst.particles.push_back(particle);
        }
        burst.textures[0]=texture(17,"particle3.raw");return insert(std::move(burst));
    }
    if(type==3){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=21)throw std::runtime_error("Spark spray requires 21 parameters");
        Instance spray;spray.type=type;
        const float defaults[]{0,2,3,0,0,0,0,0,1,6.2831854820251465f,1,1,0,1,2,.5f,.6f,.1f,3,30,60};
        std::copy(std::begin(defaults),std::end(defaults),spray.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),spray.parameters.begin());
        const auto& p=spray.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid spark spray parameter");
        if(integer(p[0])!=0)return -1;
        const auto parent=attachment_(integer(p[1]),integer(p[2]));
        spray.origin=math::transform(parent,vector(p,3,1));
        auto basis=math::identity();basis.columns[2]=math::normalize_xyz(math::transform(parent,vector(p,6)));
        basis.columns[0]=math::normalize_xyz(math::cross_xyz(basis.columns[2],{0,1,0,0}));
        if(math::dot_xyz(basis.columns[0],basis.columns[0])==0)
            basis.columns[0]=math::normalize_xyz(math::cross_xyz(basis.columns[2],{1,0,0,0}));
        basis.columns[1]=math::cross_xyz(basis.columns[0],basis.columns[2]);
        spray.radial_axis=gravity_?gravity_(spray.origin):math::Vector4{0,-1,0,0};
        const auto unit=[&]{return float(random_()&4095)/4095.f;};
        for(unsigned i=0;i<16;++i){
            auto direction=rotate_axis(basis,2,unit()*6.2831854820251465f);
            direction=rotate_axis(direction,0,unit()*p[9]-p[9]*.5f);
            Particle spark;spark.position=spray.origin;spark.direction=direction.columns[2];
            spark.speed=p[15]+(p[16]-p[15])*unit();spark.life=p[19]+(p[20]-p[19])*unit();
            spray.particles.push_back(spark);
        }
        return insert(std::move(spray));
    }
    if(type==0){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=19)throw std::runtime_error("Drifting smoke requires 19 parameters");
        Instance smoke;smoke.type=type;
        const float defaults[]{0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,.1f,1,5,90};
        std::copy(std::begin(defaults),std::end(defaults),smoke.parameters.begin());
        if(!input.empty()){
            const auto mode=integer(input[0]);
            if(mode==0)std::copy(input.begin(),input.end(),smoke.parameters.begin());
            else if(mode==1)std::copy(input.begin()+1,input.begin()+6,smoke.parameters.begin()+1);
        }
        auto& p=smoke.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid drifting smoke parameter");
        smoke.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        smoke.direction=math::transform(attachment_(integer(p[6]),integer(p[7])),vector(p,8));
        smoke.radial_axis=vector(p,11,p[14]);smoke.lifetime=p[18];p[38]=p[16];
        p[39]=float(random_()&255)/255.f*6.28f-3.14f;
        smoke.textures[0]=texture(0,"smoke1.raw");return insert(std::move(smoke));
    }
    if(type==10008){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=19)throw std::runtime_error("Smoke emitter requires 19 parameters");
        Instance emitter;emitter.type=type;
        const float defaults[]{0,0,0,0,0,0,0,0,1,0,.8f,1,2,20,0,0,0,0,0};
        std::copy(std::begin(defaults),std::end(defaults),emitter.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),emitter.parameters.begin());
        auto& p=emitter.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid smoke emitter parameter");
        for(unsigned i:{0u,1u,5u,6u})p[i]=float(integer(p[i]));
        emitter.lifetime=p[18];return insert(std::move(emitter));
    }
    if(type==11){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=13)throw std::runtime_error("Radial flash requires 13 parameters");
        Instance flash;flash.type=11;
        const float defaults[]{0,0,0,0,10,0,5,10,1,1,1,.5f,15};
        std::copy(std::begin(defaults),std::end(defaults),flash.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),flash.parameters.begin());
        const auto& p=flash.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid radial flash parameter");
        if(integer(p[0])!=0)return -1; // 19f934..19f944 rejects this mode.
        flash.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        flash.lifetime=p[12];flash.authored_mesh.emplace();auto& mesh=*flash.authored_mesh;mesh.flags=0x48000000;
        const auto color=vector(p,8,p[11]);auto edge=color;edge.w=0;
        for(unsigned plane=0;plane<4;++plane){
            auto orientation=math::identity();
            for(int axis=2;axis>=0;--axis)
                orientation=rotate_axis(orientation,axis,float(random_()&4095)/4095.f*6.2831854820251465f);
            const float radius=p[6]+(p[7]-p[6])*(float(random_()&4095)/4095.f);
            const unsigned base=unsigned(mesh.vertices.size());mesh.vertices.push_back({flash.origin,{},color,0,0});
            for(unsigned segment=0;segment<=16;++segment){
                const float angle=float(segment)*(2*std::numbers::pi_v<float>/16);
                const auto point=flash.origin+orientation.columns[0]*(std::cos(angle)*radius)+orientation.columns[1]*(std::sin(angle)*radius);
                mesh.vertices.push_back({point,{},edge,0,0});
                if(segment)mesh.indices.insert(mesh.indices.end(),{base,base+segment,base+segment+1});
            }
        }
        return insert(std::move(flash));
    }
    if(type==7){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=20)throw std::runtime_error("Smoke ribbon requires 20 parameters");
        Instance smoke;smoke.type=7;
        const float defaults[]{1,0,0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,5,10,60};
        std::copy(std::begin(defaults),std::end(defaults),smoke.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),smoke.parameters.begin());
        auto& p=smoke.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid smoke ribbon parameter");
        smoke.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        const auto direction_frame=attachment_(integer(p[6]),integer(p[7]));
        auto direction=vector(p,8);
        if(p[0]==2){
            const auto rotated=rotate_axes(direction_frame,std::span(p).subspan(8,3),false);
            direction=math::normalize_xyz(math::transform(rotated,{0,0,1,0}));
        }
        // 1a7490 applies the direction attachment in both modes, including
        // after the angular mode's normalized transformed vector.
        smoke.direction=math::transform(direction_frame,direction);smoke.direction.w=0;
        smoke.radial_axis=vector(p,11,p[14]);smoke.lifetime=p[19];
        p[20]=p[15];p[21]=p[16];p[22]=(p[17]-p[15])/p[19];p[23]=(p[18]-p[16])/p[19];
        smoke.textures[0]=texture(7,"smoke3.raw");return insert(std::move(smoke));
    }
    if(type==15){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=15)throw std::runtime_error("Expanding ring requires 15 parameters");
        Instance ring;ring.type=15;
        const float defaults[]{0,0,0,0,25,0,0,1,0,5,1,1,1,1,60};
        std::copy(std::begin(defaults),std::end(defaults),ring.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),ring.parameters.begin());
        const auto& p=ring.parameters;
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid expanding ring parameter");
        ring.origin=math::transform(attachment_(integer(p[1]),integer(p[2])),vector(p,3,1));
        auto axes=math::identity();
        if(integer(p[0])==0){
            axes.columns[1]=math::normalize_xyz(vector(p,6));
            axes.columns[2]=math::normalize_xyz(math::cross_xyz(axes.columns[1],axes.columns[0]));
            if(math::dot_xyz(axes.columns[2],axes.columns[2])==0){
                axes.columns[0]={0,1,0,0};
                axes.columns[2]=math::normalize_xyz(math::cross_xyz(axes.columns[1],axes.columns[0]));
            }
            axes.columns[0]=math::cross_xyz(axes.columns[2],axes.columns[1]);
        }else{
            // 1a1dac: apply Z, Y, X, with the engine's 16-bit angle quantization.
            axes=rotate_axes(axes,std::span(p).subspan(6,3),true);
        }
        ring.trail[0]=axes.columns[0];ring.trail[1]=axes.columns[2];
        ring.radial_axis=vector(p,10,p[13]);ring.lifetime=p[14]*.5f;
        ring.textures[0]=texture(15,"ring.raw");
        return insert(std::move(ring));
    }
    if(type==3004){
        if(!input.empty()&&input.size()!=13)throw std::runtime_error("Attached light requires 13 parameters");
        Instance light;light.type=type;const float defaults[]{0,0,0,0,0,0,0,-1,1,1,1,1,1};
        std::copy(std::begin(defaults),std::end(defaults),light.parameters.begin());if(!input.empty())std::copy(input.begin(),input.end(),light.parameters.begin());
        for(float value:light.parameters)if(!std::isfinite(value))throw std::runtime_error("Invalid attached light parameter");
        auto& p=light.parameters;const auto d=math::normalize_xyz(vector(p,5));p[5]=d.x;p[6]=d.y;p[7]=d.z;
        light.trail.fill(math::transform(attachment_(integer(p[0]),integer(p[1])),vector(p,2,1)));
        return insert(std::move(light));
    }
    if(type==4||type==10000)return create_smoke(type,input,texture(type,type==4?"smoke2.raw":"molotovfire.raw"));
    if(type==13)return create_impact(input);
    if(type==2)return create_debris(input);
    if(type==3000)return create_poison(input);
    if(type==3003)return create_body_effect(input);
    if(type==10001){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=17)throw std::runtime_error("Breakable plane requires 17 parameters");
        Instance broken;broken.type=type;
        const float defaults[]{0,49.593f,12.357f,10,-1,0,0,7.565f,9.897f,1,1,1,1,256,2,0,0};
        std::copy(std::begin(defaults),std::end(defaults),broken.parameters.begin());
        if(!input.empty()&&integer(input[0])==0)std::copy(input.begin(),input.end(),broken.parameters.begin());
        const auto& p=broken.parameters;for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Invalid breakable-plane parameter");
        const auto normal=math::normalize_xyz(vector(p,4));
        if(math::dot_xyz(normal,normal)==0||p[7]<=0||p[8]<=0)throw std::runtime_error("Invalid breakable-plane dimensions");
        auto horizontal=math::normalize_xyz(math::cross_xyz({0,1,0,0},normal));
        if(math::dot_xyz(horizontal,horizontal)==0)horizontal=math::normalize_xyz(math::cross_xyz({1,0,0,0},normal));
        const auto vertical=math::normalize_xyz(math::cross_xyz(normal,horizontal));
        const auto origin=vector(p,1,1);const auto unit=[&]{return float(random_()&4095)/4095.f;};
        std::array<math::Vector4,49> points{};
        for(unsigned row=0;row<7;++row)for(unsigned column=0;column<7;++column){
            float x=(float(column)/6-.5f)*p[7],y=(float(row)/6-.5f)*p[8];
            if(column>0&&column<6)x+=(unit()-.5f)*(p[7]/6);
            if(row>0&&row<6)y+=(unit()-.5f)*(p[8]/6);
            points[row*7+column]=origin+horizontal*x+vertical*y;
        }
        const float radius=std::min(p[7],p[8])*.5f;
        const auto add=[&](math::Vector4 a,math::Vector4 b,math::Vector4 c){
            Instance::Shard shard;shard.center=(a+b+c)*(1.f/3.f);shard.center.w=1;
            shard.local={a+shard.center*(-1),b+shard.center*(-1),c+shard.center*(-1)};
            const auto distance=shard.center+origin*(-1);shard.delay=1+30*math::dot_xyz(distance,distance)/(radius*radius);
            shard.velocity=normal*(.04f+.08f*unit())+horizontal*((unit()-.5f)*.08f)+vertical*((unit()-.5f)*.08f);
            shard.angular_velocity={(unit()-.5f)*.12f,(unit()-.5f)*.12f,(unit()-.5f)*.03f,0};
            broken.shards.push_back(shard);
        };
        for(unsigned row=0;row<6;++row)for(unsigned column=0;column<6;++column){
            const auto a=points[row*7+column],b=points[row*7+column+1],c=points[(row+1)*7+column],d=points[(row+1)*7+column+1];
            add(a,b,c);add(b,d,c);
        }
        broken.lifetime=120;return insert(std::move(broken));
    }
    if(type==6){
        if(count()==slots_.size())return -1;
        if(!input.empty()&&input.size()!=17)throw std::runtime_error("Control-panel glow requires 17 parameters");
        Instance glow;glow.type=6;const float defaults[]{0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0};
        std::copy(std::begin(defaults),std::end(defaults),glow.parameters.begin());
        if(!input.empty())std::copy(input.begin(),input.end(),glow.parameters.begin());
        for(float v:glow.parameters)if(!std::isfinite(v))throw std::runtime_error("Invalid control-panel glow parameter");
        glow.textures[0]=texture(6,"cplight.raw");return insert(std::move(glow));
    }
    if(type==1002||type==1005||type==4002||type==10007)return create_particles(type,input);
    if(!gun_type(type)&&type!=11000&&type!=11001)throw std::runtime_error("Live effect type not recovered: "+std::to_string(type));
    auto free=std::find_if(slots_.begin(),slots_.end(),[](const auto& slot){return !slot;});
    if(free==slots_.end())return -1;
    if(type==11000||type==11001){
        if(!file_loader_)throw std::runtime_error("Effect display asset loader is not connected");
        Instance effect;effect.type=type;effect.loading=std::make_shared<Display>(file_loader_(type==11000?"nowloading.cf":"cp05.cf"),type);
        *free=std::move(effect);return int(free-slots_.begin());
    }
    const unsigned expected=type==1006?41:29;
    if(!input.empty() && input.size()!=expected)throw std::runtime_error("Instance effect parameter count differs");
    Instance gun;gun.type=type;gun.parameters=defaults;
    if(!input.empty())std::copy(input.begin(),input.end(),gun.parameters.begin());
    const auto& p=gun.parameters;
    for(float value:p)if(!std::isfinite(value))throw std::runtime_error("Non-finite gun effect parameter");
    const auto parent=attachment_(integer(p[1]),integer(p[2]));
    gun.origin=math::transform(parent,vector(p,3,1));
    auto direction=p[0]==2?euler({0,0,1,0},p[6]*pi/180,p[7]*pi/180,p[8]*pi/180):vector(p,6);
    direction=math::normalize_xyz(direction);
    // 188ef4..188fe0 rotates the copied attachment basis Z, Y, X, consuming
    // exactly three effect random values. Matrix rotations postmultiply it.
    const auto jitter=[&]{return (float(random_()&4095)/4095.f*(2*pi)-pi)/64.f;};
    const float z=jitter(),y=jitter(),x=jitter();
    gun.direction=math::normalize_xyz(math::transform(parent,euler(direction,x,y,z)));
    gun.lifetime=std::max({p[15],p[22],p[28]});
    gun.textures[0]=texture(type,(std::string(prefix(type))+"flash.raw").c_str());
    gun.textures[1]=texture(type,(std::string(prefix(type))+"fire.raw").c_str());
    if(type==1006){
        gun.rays=unsigned(std::max(0,std::min(8,integer(p[29]))));
        gun.radial_axis=math::normalize_xyz(math::transform(parent,vector(p,38)));
        gun.lifetime=std::max(gun.lifetime,p[37]);gun.textures[2]=texture(type,"machinegunflash2.raw");
    }
    *free=std::move(gun);return int(free-slots_.begin());
}
void Effects::erase(int slot){if(slot>=0 && unsigned(slot)<slots_.size())slots_[unsigned(slot)].reset();}
void Effects::clear(){for(auto& slot:slots_)slot.reset();bitmaps_.clear();}
void Effects::Instance::message(int command,float value,std::optional<unsigned>){
    if(loading){
        if(type==11001){if(command==0)loading->alive=false;else if(command==2)loading->mode=integer(value);return;}
        // 1970d0: total, completed/reset interpolation, finish.
        if(command==0)loading->total=value;
        else if(command==1){loading->completed=value;loading->age=0;}
        else if(command==2)loading->completed=loading->total;
        return;
    }
    if(type==6000){auto& c=*composite;const auto& p=c.input;
        if(command==0||command==1){c.radius_from=c.radius;c.radius_to=command==0?p[22]:0;c.alpha_from=c.alpha;c.alpha_to=command==0?c.alpha:0;c.time=0;c.duration=command==0?p[23]:p[25];c.dying=command==1;c.active=command==0;}return;}
    if(type==6001||type==2002||type==-67||type==-68||type==-69||type==-70)return; // 19e9e8 / 1a5df8.
    if(type==11002){if(command==0)parameters[0]=value;return;} // 1aaf48
    if(type==5001){if(command==0&&parameters[27]==0){parameters[27]=1;age=0;parameters[23]=value;parameters[11]=parameters[24];parameters[12]=parameters[25];parameters[18]=parameters[26];}return;} // 192410
    if(type==4003){if(command==500)parameters[5]=value;return;} // 192bd8: update emitter scale.
    if(type==3000||type==-64)return; // Empty poison callbacks.
    if(type==10012||type==10005||type==10004||type==12||type==16||type==10011||type==10002||type==4000||type==-66||type==-65||type==2||type==13||type==10006||type==7000||type==10003||type==9||type==5||type==0||type==3||type==4||type==7||type==8||type==11||type==14||type==15||type==17||type==10000||type==10001||type==10008||type==10009||type==10010)return; // Empty original callbacks.
    if(type==6)return; // Payload commands are applied by 1012 (17b370).
    if(type==3004){if(command==200)parameters[12]=value;return;}
    if(type==3003 || authored_mesh)return; // Empty callbacks at 177288 / 177e98 / 178740.
    // All four recovered weapon vtables use the empty callback at 189a40.
    // Do not extend this behavior to other effect classes when adding them.
    if(!gun_type(type)&&type!=1002&&type!=1005&&type!=4002&&type!=10007&&type>=0)throw std::runtime_error("Effect message handler not recovered: "+std::to_string(type));
}
unsigned Effects::message(int slot,int command,float value,std::optional<unsigned> payload){
    // 1f5948: any out-of-range index broadcasts, including positive overflow.
    // Addressing a free in-range slot does not broadcast.
    if(slot>=0 && unsigned(slot)<slots_.size()){
        auto& effect=slots_[unsigned(slot)];if(!effect)return 0;
        if(effect->type==5000)cancel_heron(*effect);else effect->message(command,value,payload);return 1;
    }
    unsigned delivered=0;
    for(auto& effect:slots_)if(effect){if(effect->type==5000)cancel_heron(*effect);else effect->message(command,value,payload);++delivered;}
    return delivered;
}
unsigned Effects::count()const{return unsigned(std::count_if(slots_.begin(),slots_.end(),[](const auto& slot){return slot.has_value();}));}
void Effects::advance(float frames){
    if(!std::isfinite(frames)||frames<0)throw std::runtime_error("Invalid effect update duration");
    for(auto& slot:slots_)if(slot){
        if(slot->type==10001){
            slot->age+=frames;if(slot->age>slot->lifetime){slot.reset();continue;}
            for(auto& shard:slot->shards)if(slot->age>=shard.delay){
                shard.center=shard.center+shard.velocity*frames;shard.velocity.y-=.02f*frames;
                shard.rotation=shard.rotation+shard.angular_velocity*frames;
            }
            continue;
        }
        if(slot->type==10009){
            // 170d00 subtracts once, strictly above one, even for large deltas.
            slot->parameters[0]+=frames*std::bit_cast<float>(0x3b088889u);
            slot->parameters[1]+=frames*std::bit_cast<float>(0x3a888889u);
            for(unsigned i=0;i<2;++i)if(slot->parameters[i]>1)slot->parameters[i]-=1;
            continue;
        }
        if(slot->type==7000){if(!advance_shell(*slot,frames))slot.reset();continue;}
        if(slot->type==10006){if(!advance_wave(*slot,frames))slot.reset();continue;}
        if(slot->type==10003){slot.reset();continue;}
        if(slot->type==9){
            if(slot->explosion_child==Instance::ExplosionChild::none){slot.reset();continue;}
            slot->age+=frames;if(slot->age>slot->lifetime){slot.reset();continue;}
            auto& p=slot->parameters;
            if(slot->explosion_child==Instance::ExplosionChild::spark){p[34]+=p[36];p[35]+=p[37];p[36]+=p[14]*.003f;}
            p[32]+=p[33];p[33]*=.9f;continue;
        }
        if(slot->type==5){slot->age+=std::bit_cast<float>(0x38b702d4u);if(slot->age>2*pi)slot->age-=2*pi;continue;}
        if(slot->type==10010){
            for(auto& ray:slot->particles){ray.age+=frames;ray.color.w=ray.color.w==slot->parameters[10]?slot->parameters[11]:slot->parameters[10];}
            slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;
        }
        if(slot->type==14){
            slot->age+=frames;unsigned alive=0;
            for(auto& smoke:slot->particles)if(slot->age<smoke.life){
                smoke.angle+=.03490658476948738f;if(smoke.angle>6.2831854820251465f)smoke.angle-=6.2831854820251465f;
                smoke.position=smoke.position+smoke.direction;++alive;
            }
            if(!alive)slot.reset();continue;
        }
        if(slot->type==8){
            slot->age+=frames;auto& p=slot->parameters;
            if(slot->age>p[17]){
                const auto unit=[&]{return float(random_()&4095)/4095.f;};
                for(unsigned i=1;i<15;++i){
                    const float x=(.5f-unit())*p[15],y=(.5f-unit())*p[15],z=(.5f-unit())*p[15];
                    slot->particles[i].direction={x,y,z,0};slot->particles[i].position={};
                }
                slot->age=0;p[17]=unit()*7+1;p[18]=unit()*.45f+.5f;p[19]=float(random_()&3)*.25f;
            }else for(unsigned i=1;i<15;++i){auto& point=slot->particles[i];point.position=point.position*p[18]+point.direction*(1-p[18]);}
            continue;
        }
        if(slot->type==17){
            slot->age+=frames;unsigned alive=0;
            for(auto& particle:slot->particles)if(slot->age<particle.life){
                particle.position=particle.position+particle.direction*frames;
                particle.direction=particle.direction+slot->radial_axis*(frames*.01f);++alive;
            }
            if(!alive)slot.reset();continue;
        }
        if(slot->type==3){
            unsigned alive=0;
            for(auto& spark:slot->particles)if(spark.age<=spark.life){
                spark.age+=frames;const auto travel=spark.direction*(frames*spark.speed);
                spark.position=spark.position+travel;
                const auto velocity=travel+slot->radial_axis*.01f;
                // 19b288..19b2b0 updates speed only; it does not replace direction.
                spark.speed=std::sqrt(math::dot_xyz(velocity,velocity));++alive;
            }
            if(!alive)slot.reset();continue;
        }
        if(slot->type==0||slot->type==-67){
            slot->age+=frames;if(slot->age>slot->lifetime){slot.reset();continue;}
            auto& p=slot->parameters;slot->origin=slot->origin+slot->direction*(frames*p[15]);
            const float progress=slot->age/slot->lifetime;
            p[38]=p[16]+progress*p[17];slot->radial_axis.w=p[14]*(1-progress);
            if(slot->type==-67){p[15]*=.95f;p[39]+=std::bit_cast<float>(0x3bc90fdbu);}continue;
        }
        if(slot->type==10008){emit_smoke(*slot);slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==7){
            if(slot->age>=slot->lifetime){slot.reset();continue;}
            slot->age+=frames;slot->parameters[20]+=slot->parameters[22];slot->parameters[21]+=slot->parameters[23];continue;
        }
        if(slot->type==15||slot->type==13){slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==2){if(!advance_debris(*slot,frames))slot.reset();continue;}
        if(slot->type==3000){if(!advance_poison(*slot,frames))slot.reset();continue;}
        if(slot->type==-64){if(!advance_poison_impact(*slot,frames))slot.reset();continue;}
        if(slot->type==11002){slot->age+=std::bit_cast<float>(0x393702d4u);if(slot->age>2*pi)slot->age-=2*pi;continue;}
        if(slot->type==10011){auto& p=slot->parameters;p[1]+=std::bit_cast<float>(0x3a490fdbu);if(p[1]>std::bit_cast<float>(0x3ec90fdbu))p[1]-=std::bit_cast<float>(0x3ec90fdbu);p[0]=p[0]<19.9f?p[0]*.8f+4:20;continue;}
        if(slot->type==5000){if(!advance_heron(*slot,frames))slot.reset();continue;}
        if(slot->type==-68){if(!advance_heron_ball(*slot,frames))slot.reset();continue;}
        if(slot->type==-69||slot->type==-70){auto& c=*slot->composite;if(slot->type==-70){c.radius+=c.duration;c.radius_to+=c.duration;}slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==2002){slot->age+=frames;bool alive=false;for(auto& p:slot->particles)if(slot->age<p.life){alive=true;p.angle+=p.growth;}if(!alive)slot.reset();continue;}
        if(slot->type==6000){if(!advance_plant(*slot,frames))slot.reset();continue;}
        if(slot->type==6001){if(!advance_plant_pillar(*slot,frames))slot.reset();continue;}
        if(slot->type==10002){if(!advance_energy_projectile(*slot,frames))slot.reset();continue;}
        if(slot->type==5001){if(!advance_lightning_emitter(*slot,frames))slot.reset();continue;}
        if(slot->type==-66){slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==10012){if(!advance_star_burst(*slot,frames))slot.reset();continue;}
        if(slot->type==10005){if(!advance_energy_trail(*slot,frames))slot.reset();continue;}
        if(slot->type==10004){advance_bubbles(*slot);continue;}
        if(slot->type==12){
            for(auto& particle:slot->particles){particle.angle+=frames*std::bit_cast<float>(0x3c567750u);if(particle.angle>2*pi)particle.angle-=2*pi;}
            continue;
        }
        if(slot->type==16){if(!advance_encounter(*slot,frames))slot.reset();continue;}
        if(slot->type==4003){advance_omen(*slot,frames);continue;}
        if(slot->type==4||slot->type==10000||slot->type==-65){
            auto& p=slot->parameters;slot->age+=frames;
            if(slot->age>slot->lifetime){slot.reset();continue;}
            // 1a6790: age uses dt; authored increments are per update.
            if(slot->age>p[31]&&slot->age<=p[31]+frames){
                p[37]=p[26];p[36]=(p[29]-p[28])/(p[30]-p[31]);
                for(unsigned i=0;i<4;++i)p[32+i]=(p[21+i]-p[17+i])/(p[30]-p[31]);
            }
            p[38]+=p[36];slot->radial_axis=slot->radial_axis+vector(p,32,p[35]);
            slot->origin=slot->origin+slot->direction*(p[37]*(slot->type==10000?frames:1.f));continue;
        }
        if(slot->type==3004){emit_trail(*slot);continue;}
        if(slot->type==-62||slot->type==-63){slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==3003){slot->age+=frames;emit_body_effect(*slot);continue;}
        if(slot->authored_mesh){slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();continue;}
        if(slot->type==6){slot->age=std::fmod(slot->age+frames,3.f);continue;}
        if(slot->type==1002||slot->type==1005||slot->type==4002||slot->type==10007||slot->type<0){if(!advance_particles(*slot,frames))slot.reset();continue;}
        if(slot->loading){auto& display=*slot->loading;
            if(slot->type==11001){if(!display.alive){slot.reset();continue;}display.level=microphone_held_?std::min(1.f,std::abs(float(microphone_sample_))/8192.f):0;display.player.pointer(microphone_held_?220.f:520.f,79,false);display.player.advance();continue;}
            display.age+=frames;
            if(display.completed>=display.total)slot.reset();
            else display.player.advance();
            continue;
        }
        for(unsigned i=0;i<slot->rays;++i)slot->jitter[i]=float(random_()&4095)/4095.f;
        slot->age+=frames;if(slot->age>slot->lifetime)slot.reset();
    }
    for(auto it=bitmaps_.begin();it!=bitmaps_.end();)if(it->second.expired())it=bitmaps_.erase(it);else ++it;
}
Bindings Effects::bindings(){
    Bindings result;
    result.emplace(1080,Binding{5,0,[this](const Invocation& c){
        // 113580 snapshots actor/object world matrix (+a0), then copies each
        // requested column. A zero output address leaves that column untouched.
        const int source=integer(c.arguments[0]);if(source<2||source>=35)throw std::runtime_error("Invalid world matrix selector");
        const auto matrix=attachment_(source,0);
        for(unsigned column=0;column<4;++column){const int word=integer(c.arguments[column+1]);if(!word)continue;
            if(word<0||!c.state||!c.state->data_contains(c.program,unsigned(word),4))throw std::runtime_error("World matrix column outside live script data");
            const auto v=matrix.columns[column];const std::array<float,4> values{v.x,v.y,v.z,v.w};
            for(unsigned i=0;i<4;++i)(*c.state->data)[unsigned(word)+i]=values[i];
        }
        return Reply{false,0.f};
    }});
    result.emplace(1081,Binding{4,0,[this](const Invocation& c){
        const auto address=[&](float value){
            const double bytes=std::trunc(double(value*4.f+std::copysign(.5f,value)));
            if(!std::isfinite(bytes)||bytes<0||bytes>2147483647.||std::fmod(bytes,4)!=0||!c.state)throw std::runtime_error("Invalid attachment vector address");
            const unsigned word=unsigned(bytes)/4;
            if(!c.state->data_contains(c.program,word,4))throw std::runtime_error("Attachment vector outside live data");return word;
        };
        const auto destination=address(c.arguments[0]),source=address(c.arguments[3]);
        std::array<float,4> values{};for(unsigned i=0;i<4;++i){const auto it=c.state->data->find(source+i);values[i]=it==c.state->data->end()?std::bit_cast<float>(c.program.word(source+i)):it->second;}
        // 1137e0 snapshots the input before applying 18d7b8's attachment.
        const auto result=math::transform(attachment_(integer(std::nearbyint(c.arguments[1])),integer(std::nearbyint(c.arguments[2]))),{values[0],values[1],values[2],values[3]});
        values={result.x,result.y,result.z,result.w};for(unsigned i=0;i<4;++i)(*c.state->data)[destination+i]=values[i];
        return Reply{false,0.f};
    }});
    for(unsigned opcode:{1072u,1073u,1074u,1075u})result.emplace(opcode,Binding{3,0,[this,opcode](const Invocation& call){
        for(float value:call.arguments)integer(value);
        const int destination=int(std::nearbyint(call.arguments[2]));
        if(destination<0||!call.state||!call.state->data_contains(call.program,unsigned(destination),4))throw std::runtime_error("Effect attachment output outside live script data");
        // 113280/113380/113480 copy basis X, basis Z, and translation.
        const auto position=attachment_(int(std::nearbyint(call.arguments[0])),int(std::nearbyint(call.arguments[1]))).columns[opcode==1072?1:opcode==1073?0:opcode==1074?2:3];
        const std::array<float,4> values{position.x,position.y,position.z,position.w};
        for(unsigned i=0;i<4;++i)(*call.state->data)[unsigned(destination)+i]=values[i];
        return Reply{false,0.f};
    }});
    result.emplace(1009,Binding{2,0,[this](const Invocation& call){
        const int type=integer(call.arguments[0]);
        if(!gun_type(type)&&type!=5000&&type!=2002&&type!=6000&&type!=6001&&type!=11002&&type!=10011&&type!=11001&&type!=10002&&type!=5001&&type!=10012&&type!=10005&&type!=10004&&type!=12&&type!=16&&type!=4000&&type!=4003&&type!=2&&type!=13&&type!=10006&&type!=7000&&type!=10003&&type!=9&&type!=5&&type!=0&&type!=3&&type!=4&&type!=6&&type!=7&&type!=8&&type!=11&&type!=14&&type!=15&&type!=17&&type!=11000&&type!=1002&&type!=1005&&type!=4002&&type!=10007&&type!=10008&&type!=10000&&type!=10001&&type!=10010&&type!=10009&&type!=3003&&type!=3004&&type!=3000)throw std::runtime_error("Live effect type not recovered: "+std::to_string(type));
        std::vector<float> values;
        if(call.arguments[1]!=0&&type!=11000&&type!=11001&&type!=11002&&type!=10011&&type!=5&&type!=10009){
            const auto address=integer(call.arguments[1]);unsigned count=type==5000?34:type==2002?24:type==6000?76:type==6001?44:type==10002?18:type==5001?24:type==10012?6:type==10005?15:type==10004?13:type==12?11:type==16?17:type==4000?5:type==4003?6:type==2?21:type==13?20:type==10006?13:type==7000?35:type==10003?12:type==9?13:type==10010?18:type==14?27:type==8?17:(type==3||type==17)?21:(type==0||type==10008)?19:type==11?13:type==7?20:type==15?15:type==3000?21:type==3004?13:(type==4||type==10000)?32:type==3003?3:(type==6||type==10001)?17:type==10007?24:type==1002?19:type==1005?13:type==4002?12:type==1006?41:29;
            if(type==3003&&address>=0&&(call.state?call.state->data_contains(call.program,unsigned(address)):unsigned(address)<call.program.size())){
                const auto mode=call.state&&call.state->data->contains(unsigned(address))?call.state->data->at(unsigned(address)):std::bit_cast<float>(call.program.word(unsigned(address)));
                if(mode==3)count=14;
            }
            if(type==4002&&address>=0&&(call.state?call.state->data_contains(call.program,unsigned(address)):unsigned(address)<call.program.size())){
                const auto mode=call.state&&call.state->data->contains(unsigned(address))?call.state->data->at(unsigned(address)):std::bit_cast<float>(call.program.word(unsigned(address)));
                if(mode==1)count=6;
            }
            if(address<0 || (call.state?!call.state->data_contains(call.program,unsigned(address),count):std::uint64_t(address)+count>call.program.size()))throw std::runtime_error("Instance effect payload outside script data");
            for(unsigned i=0;i<count;++i){
                const auto word=unsigned(address)+i;
                if(call.state && call.state->data->contains(word))values.push_back(call.state->data->at(word));
                else values.push_back(std::bit_cast<float>(call.program.word(word)));
            }
        }
        return Reply{false,float(create(type,values))};
    }});
    result.emplace(1010,Binding{1,0,[this](const Invocation& call){erase(integer(call.arguments[0]));return Reply{false,0.f};}});
    result.emplace(1011,Binding{0,0,[this](const Invocation&){clear();return Reply{false,0.f};}});
    result.emplace(1012,Binding{4,0,[this](const Invocation& call){
        std::optional<unsigned> payload;
        if(call.arguments[3]>=1){
            const auto address=integer(call.arguments[3]);
            if(address<0 || (call.state?!call.state->data_contains(call.program,unsigned(address)):unsigned(address)>=call.program.size()))throw std::runtime_error("Effect message payload outside script data");
            payload=unsigned(address);
        }
        const auto target=integer(call.arguments[0]),command=integer(call.arguments[1]);
        for(unsigned i=0;i<slots_.size();++i)if((target<0||target>=int(slots_.size())||target==int(i))&&slots_[i]&&slots_[i]->type==6&&(command==0||command==2)){
            const unsigned count=command==0?5:4;
            if(!payload||(call.state?!call.state->data_contains(call.program,*payload,count):std::uint64_t(*payload)+count>call.program.size()))throw std::runtime_error("Control-panel glow message outside script data");
            std::array<float,5> data{};
            for(unsigned j=0;j<count;++j){const auto word=*payload+j;data[j]=call.state&&call.state->data->contains(word)?call.state->data->at(word):std::bit_cast<float>(call.program.word(word));if(!std::isfinite(data[j]))throw std::runtime_error("Invalid glow message");}
            std::copy_n(data.begin(),count,slots_[i]->parameters.begin()+(command==0?1:11));
        }
        for(unsigned i=0;i<slots_.size();++i)if((target<0||target>=int(slots_.size())||target==int(i))&&slots_[i]&&slots_[i]->type==3004&&command>=0&&command<=2){
            const unsigned count=command==2?4:5;
            if(!payload||(call.state?!call.state->data_contains(call.program,*payload,count):std::uint64_t(*payload)+count>call.program.size()))throw std::runtime_error("Attached light message outside script data");
            std::array<float,5> data{};for(unsigned j=0;j<count;++j){const auto word=*payload+j;data[j]=call.state&&call.state->data->contains(word)?call.state->data->at(word):std::bit_cast<float>(call.program.word(word));if(!std::isfinite(data[j]))throw std::runtime_error("Invalid attached light message");}
            auto& p=slots_[i]->parameters;
            if(command==2){p[8]=data[0];p[9]=data[2];p[10]=data[1];p[11]=data[3];}
            else {
                const auto matrix=attachment_(integer(std::nearbyint(data[0])),integer(std::nearbyint(data[1])));
                const auto value=math::transform(matrix,{data[2],data[3],data[4],command==0?1.f:0.f});
                if(command==0){p[0]=data[0];p[1]=data[1];p[2]=value.x;p[3]=value.y;p[4]=value.z;}
                else {p[5]=value.x;p[6]=value.y;p[7]=value.z;}
            }
        }
        for(unsigned i=0;i<slots_.size();++i)if((target<0||target>=int(slots_.size())||target==int(i))&&slots_[i]&&slots_[i]->type==11001&&command==1){
            if(!payload||(call.state?!call.state->data_contains(call.program,*payload,2):std::uint64_t(*payload)+2>call.program.size()))throw std::runtime_error("Microphone display position outside script data");
            float xy[2];for(unsigned j=0;j<2;++j){const auto word=*payload+j;xy[j]=call.state&&call.state->data->contains(word)?call.state->data->at(word):std::bit_cast<float>(call.program.word(word));if(!std::isfinite(xy[j]))throw std::runtime_error("Invalid microphone display position");}
            slots_[i]->loading->x=320+xy[0];slots_[i]->loading->y=224+xy[1];
        }
        message(target,command,call.arguments[2],payload);return Reply{false,0.f};
    }});
    return result;
}
}
