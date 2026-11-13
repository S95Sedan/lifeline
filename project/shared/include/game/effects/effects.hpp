#pragma once
#include "game/effects/effect_resources.hpp"
#include "rendering/model.hpp"
#include "assets/flash.hpp"
#include <algorithm>
#include <array>

namespace lifeline::script {
// Native live effect instances, separate from the classes' static resources.
class Effects {
public:
    struct Contact {math::Vector4 point{},normal{};int material=0;};
    using Attachment=std::function<math::Matrix4(int,int)>;
    struct Display {
        FlashMovie movie;
        FlashPlayer player;
        float total=1,completed=0,age=0,x=154,y=158,level=0;
        int type=11000,mode=0;bool alive=true;
        explicit Display(const Bytes& bytes,int kind=11000):movie(bytes,kind==11000?"nowloading.cf":"cp05.cf"),player(movie),type(kind){}
        std::vector<FlashDraw> compose()const;
    };
private:
    struct Particle {
        enum Kind {fragment,smoke,flash,star,ring,spray} kind=smoke;
        math::Vector4 position{},direction{},color{1,1,1,1};
        math::Vector4 rotation{},angular_velocity{};
        float age=0,life=1,speed=0,size=0,growth=0,angle=0,extent=0,far_size=0,initial_growth=0;
        std::shared_ptr<const AssetImage> texture;
    };
    struct Instance {
        struct Shard {
            std::array<math::Vector4,3> local{};
            math::Vector4 center{},velocity{},rotation{},angular_velocity{};
            float delay=0;
        };
        struct Composite {
            std::array<float,76> input{};
            float radius=0,alpha=0,radius_from=0,radius_to=0,alpha_from=0,alpha_to=0,time=0,duration=0,trail_time=0;
            bool active=false,dying=false,flying=false,emitting=false;
            int phase=1,ball=-1,lightning=-1;
            math::Vector4 previous{},velocity{},fade_velocity{};
            math::Matrix4 basis=math::identity();
        };
        std::optional<Composite> composite;
        enum class ExplosionChild {none, fire, bomb, spark, star};
        ExplosionChild explosion_child=ExplosionChild::none;
        int type=1000;
        std::array<float,41> parameters{};
        math::Vector4 origin{},direction{},radial_axis{};
        float age=0,lifetime=0;
        unsigned rays=0;
        unsigned aura_angle=0,aura_step=0;
        std::array<float,8> jitter{};
        std::array<std::shared_ptr<const AssetImage>,3> textures;
        std::shared_ptr<Display> loading;
        std::vector<Particle> particles;
        std::vector<Shard> shards;
        std::optional<ModelMesh> authored_mesh;
        std::array<math::Vector4,3> trail{};
        void message(int command,float value,std::optional<unsigned> payload);
    };
    std::int16_t microphone_sample_=0;bool microphone_held_=false;
    EffectResources& resources_;
    Attachment attachment_;
    std::function<std::uint32_t()> random_;
    std::function<Bytes(const std::string&)> file_loader_;
    std::array<std::optional<Instance>,256> slots_;
    std::map<std::shared_ptr<const Bytes>,std::weak_ptr<const AssetImage>,std::owner_less<std::shared_ptr<const Bytes>>> bitmaps_;
    std::shared_ptr<const AssetImage> texture(int type,const char* member);
    int create_smoke(int,std::span<const float>,std::shared_ptr<const AssetImage>);
    std::function<std::optional<Contact>(math::Vector4,float,int,int)> surface_contact_;
    std::function<std::optional<int>(math::Vector4,math::Vector4,float,int,int)> capsule_contact_;
    int create_title_environment();
    int create_heron(std::span<const float>);
    bool advance_heron(Instance&,float);
    bool advance_heron_ball(Instance&,float);
    void cancel_heron(Instance&);
    void create_heron_arc(const Instance&,bool);
    int create_damage_wave(std::span<const float>);
    std::vector<ModelMesh> heron_geometry(const Instance&,const SceneCamera&,std::array<unsigned,3>)const;
    int create_plant(std::span<const float>);
    int create_plant_pillar(std::span<const float>);
    bool advance_plant(Instance&,float);
    bool advance_plant_pillar(Instance&,float);
    std::vector<ModelMesh> plant_geometry(const Instance&,const SceneCamera&,std::array<unsigned,3>)const;
    int create_plant_smoke(std::span<const float>,bool);
    std::function<float(math::Vector4)> ground_height_;
    std::vector<ModelMesh> title_environment_geometry(const Instance&,const SceneCamera&,std::array<unsigned,3>,unsigned,unsigned)const;
    int create_energy_projectile(std::span<const float>);
    bool advance_energy_projectile(Instance&,float);
    std::vector<ModelMesh> energy_projectile_geometry(const Instance&)const;
    int create_lightning_emitter(std::span<const float>);
    bool advance_lightning_emitter(Instance&,float);
    int create_lightning_ribbon(std::span<const float>,std::shared_ptr<const AssetImage>);
    ModelMesh lightning_ribbon_geometry(const Instance&,unsigned)const;
    int create_star_burst(std::span<const float>);
    bool advance_star_burst(Instance&,float);
    int create_energy_trail(std::span<const float>);
    bool advance_energy_trail(Instance&,float);
    std::vector<ModelMesh> energy_trail_geometry(const Instance&,const SceneCamera&,unsigned)const;
    int create_bubbles(std::span<const float>);
    void advance_bubbles(Instance&);
    int create_orbit_light(std::span<const float>);
    int create_encounter(std::span<const float>);
    bool advance_encounter(Instance&,float);
    int create_dust(std::span<const float>);
    int create_omen(std::span<const float>);
    void advance_omen(Instance&,float);
    int create_particles(int type,std::span<const float> parameters);
    int insert(Instance effect);
    bool advance_particles(Instance&,float);
    std::function<math::Vector4()> viewpoint_;
    std::function<math::Vector4(math::Vector4)> gravity_;
    std::function<math::Vector4(math::Vector4)> ground_;
    std::function<std::optional<int>(math::Vector4,float,int,int)> overlap_;
    std::function<void(unsigned,int,int,int)> post_;
    int create_poison(std::span<const float>);
    bool advance_poison(Instance&,float);
    bool advance_poison_impact(Instance&,float);
    void emit_trail(Instance&);
    void emit_smoke(Instance&);
    int create_explosion(std::span<const float>);
    int create_impact(std::span<const float>);
    int create_debris(std::span<const float>);
    bool advance_debris(Instance&,float);
    ModelMesh debris_geometry(const Instance&)const;
    int create_burst(std::span<const float>);
    int create_shell(std::span<const float>);
    int create_wave(std::span<const float>);
    bool advance_wave(Instance&,float);
    std::vector<ModelMesh> wave_geometry(const Instance&,const SceneCamera&)const;
    bool advance_shell(Instance&,float);
    std::vector<ModelMesh> shell_geometry(const Instance&,std::array<unsigned,3>)const;
    void create_explosion_sprite(std::span<const float>,bool spark);
    ModelMesh explosion_geometry(const Instance&,const SceneCamera&,unsigned texture,float turn=1)const;
    int create_body_effect(std::span<const float>);
    void emit_body_effect(Instance&);
    void emit_body_ring(std::span<const float>,const std::shared_ptr<const AssetImage>&);
    void emit_body_strip(int,int,bool,const std::shared_ptr<const AssetImage>&);
public:
    Effects(EffectResources& resources,Attachment attachment,std::function<std::uint32_t()> random)
        :resources_(resources),attachment_(std::move(attachment)),random_(std::move(random)){}
    void microphone(std::int16_t sample,bool held){microphone_sample_=sample;microphone_held_=held;}
    void viewpoint(std::function<math::Vector4()> eye){viewpoint_=std::move(eye);}
    void gravity(std::function<math::Vector4(math::Vector4)> direction){gravity_=std::move(direction);}
    void ground(std::function<math::Vector4(math::Vector4)> project){ground_=std::move(project);}
    void ground_height(std::function<float(math::Vector4)> height){ground_height_=std::move(height);}
    void collision(std::function<std::optional<int>(math::Vector4,float,int,int)> overlap,
                   std::function<void(unsigned,int,int,int)> post){overlap_=std::move(overlap);post_=std::move(post);}
    void contacts(std::function<std::optional<Contact>(math::Vector4,float,int,int)> surface,
                  std::function<std::optional<int>(math::Vector4,math::Vector4,float,int,int)> capsule){surface_contact_=std::move(surface);capsule_contact_=std::move(capsule);}
    int create(int type,std::span<const float> parameters={});
    void erase(int slot);
    void clear();
    // Restore the Start-page alpha mask without restarting the animated sky.
    void title_start_page(){for(auto& slot:slots_)if(slot&&slot->type==11002)slot->parameters[0]=0;}
    unsigned message(int slot,int command,float value,std::optional<unsigned> payload={});
    void advance(float frames=1);
    bool active(unsigned slot)const{return slot<slots_.size() && slots_[slot].has_value();}
    bool active_type(int type)const{return std::any_of(slots_.begin(),slots_.end(),[type](const auto& slot){return slot&&slot->type==type&&(!slot->loading||slot->loading->alive);});}
    unsigned count()const;
#ifdef LIFELINE_DEV_SCENE_PROBE
    auto debug_instances()const{return slots_;}
#endif
    StaticModel geometry(const SceneCamera&,bool include_title_environment=true,unsigned viewport_width=640,unsigned viewport_height=448)const;
    Bindings bindings();
    void file_loader(std::function<Bytes(const std::string&)> loader){file_loader_=std::move(loader);}
    std::vector<std::shared_ptr<const Display>> displays()const;
    std::vector<FlashDraw> procedural_displays(bool extend_background=false)const;
};
}
