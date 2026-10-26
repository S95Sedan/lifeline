#include "game/effects/effect_resources.hpp"
#include <cmath>
#include <climits>
#include <functional>

namespace lifeline::script {
namespace {
struct Definition {const char* member;unsigned width,height,pixel_format,usage;bool palette;};
// Recovered static class resources and dependency order; evidence in effect-resources-121.md.
const Definition definitions[]={
    {"smoke1.raw",64,64,2,8,false},
    {"smoke1.raw",64,64,2,8,false},
    {"smoke2.raw",64,64,2,8,false},
    {"sky01.raw",512,512,19,0,false},
    {"sky01.clut",256,1,0,0,true},
    {"cplight.raw",32,32,2,8,false},
    {"smoke3.raw",64,64,0,8,false},
    {"thunder.raw",128,128,2,8,false},
    {"pointlight.raw",64,64,2,8,false},
    {"pointlight3.raw",64,64,2,8,false},
    {"explosion2ring.raw",128,128,2,8,false},
    {"smoke4.raw",64,64,2,8,false},
    {"ring.raw",64,64,2,8,false},
    {"encount_ball.raw",64,64,2,8,false},
    {"particle3.raw",64,64,2,8,false},
    {"handgunflash.raw",32,32,2,8,false},
    {"handgunfire.raw",32,32,2,8,false},
    {"bloodspray.raw",64,64,2,8,false},
    {"lasersmoke.raw",64,64,2,8,false},
    {"machinegunfire.raw",32,32,2,8,false},
    {"machinegunflash.raw",32,32,2,8,false},
    {"machinegunflash2.raw",32,32,2,8,false},
    {"shotgunfire.raw",32,32,2,8,false},
    {"shotgunflash.raw",32,32,2,8,false},
    {"launcherfire.raw",32,32,2,8,false},
    {"launcherflash.raw",32,32,2,8,false},
    {"damagewave_flash.raw",128,128,2,8,false},
    {"damagewave_thunder.raw",128,128,2,8,false},
    {"damagewave_particle.raw",64,64,2,8,false},
    {"poisondebris.raw",64,64,0,8,false},
    {"poisonsmoke.raw",64,64,0,8,false},
    {"reflex.raw",64,64,2,6,false},
    {"lee_star.raw",64,64,2,8,false},
    {"lee_ring.raw",64,64,2,8,false},
    {"plantpillar.raw",128,128,2,8,false},
    {"plantball.raw",64,64,2,8,false},
    {"shellring.raw",128,128,2,8,false},
    {"shellthunder.raw",128,128,2,8,false},
    {"shellringedge.raw",64,64,2,8,false},
    {"molotovfire.raw",64,64,2,8,false},
    {"bubble.raw",64,64,2,8,false},
    {"lee_ring.raw",64,64,2,8,false},
    {"",256,256,0,8,false},
    {"camerasmoke.raw",128,128,2,8,false},
    {"explosionlight.raw",64,64,2,8,false},
    {"star_flare05.raw",64,64,2,8,false},
    {"ring04.raw",64,64,2,8,false},
    {"bomb1.raw",128,128,2,8,false},
    {"fire.raw",64,64,2,8,false},
    {"spark1.raw",64,64,2,8,false},
    {"bloodsmoke.raw",64,64,0,8,false},
    {"cat_tpipe.raw",64,64,2,8,false},
    {"leeomen.raw",64,64,2,8,false},
    {"heronthunder3.raw",128,128,2,8,false},
    {"heronthunder5.raw",64,64,2,8,false},
    {"heronthunder1.raw",128,128,2,8,false},
    {"heronball.raw",64,64,2,8,false},
    {"plantsmoke.raw",64,64,2,8,false},
    {"spark.raw",64,64,2,8,false},
    {"logoBG_A.raw",512,256,19,8,false},
    {"logoBG_B.raw",512,256,19,8,false},
    {"logoBG_A.clut",256,1,0,8,true},
};
// Nonnegative entries acquire a texture owner; negative entries call a child initializer.
const std::vector<int> recipes[]={
    {0}, // 0x1a5270
    {1}, // 0x186118
    {}, // 0x199fe8
    {}, // 0x19add8
    {2}, // 0x1a5f48
    {3,4}, // 0x1a8d00
    {5}, // 0x17a8b8
    {6}, // 0x1a6fa8
    {7}, // 0x1ab040
    {-56,-57,-58,-5}, // 0x17e3f8
    {8}, // 0x19ebf0
    {}, // 0x19f880
    {9}, // 0x19ff78
    {10,-3,-4}, // 0x181138
    {11}, // 0x1a7c90
    {12}, // 0x1a1970
    {13}, // 0x17ce10
    {14}, // 0x19b6c0
    {15,16}, // 0x188848
    {}, // 0x170238
    {-59,17}, // 0x186b10
    {18}, // 0x18bd20
    {-1,-5}, // 0x18cbf0
    {19,20,21}, // 0x189a48
    {22,23}, // 0x18a8f8
    {24,25}, // 0x18b568
    {26,27,28,-18}, // 0x17b4d8
    {-32,29,30}, // 0x171708
    {31,-1}, // 0x173338
    {-60,-61}, // 0x175540
    {}, // 0x178930
    {-5}, // 0x17a000
    {-5}, // 0x193cd0
    {}, // 0x195758
    {32,33,-2}, // 0x194238
    {-62}, // 0x1927e0
    {-63,-64,-65,-66}, // 0x18e8d8
    {-63}, // 0x191fd8
    {-11,-40,-67,-12}, // 0x19c2f8
    {34,35,-1}, // 0x19d6f8
    {36,37,38}, // 0x1a3530
    {39}, // 0x198798
    {}, // 0x1971f8
    {-57,-68,-5}, // 0x182ca0
    {-69}, // 0x1a2340
    {40}, // 0x16f7c8
    {41}, // 0x16d198
    {42}, // 0x1a0c40
    {}, // 0x185860
    {-5}, // 0x17c750
    {43}, // 0x170aa0
    {44}, // 0x181d90
    {}, // 0x1ac3c8
    {45,46}, // 0x16e3e0
    {59,60,61,3,4}, // Native retention of textures read by 1a9fe0 each draw.
    {47}, // 0x17f890
    {48}, // 0x183788
    {49}, // 0x180498
    {50}, // 0x1879e8
    {51}, // 0x177290
    {-60}, // 0x177ea0
    {52}, // 0x192bf0
    {53}, // 0x18f200
    {54}, // 0x18fd08
    {55}, // 0x190490
    {56}, // 0x190bd0
    {57}, // 0x19d350
    {58}, // 0x184700
    {-1}, // 0x1a3218
};
const std::map<int,unsigned> types={
    {0,0},
    {1,1},
    {2,2},
    {3,3},
    {4,4},
    {5,5},
    {6,6},
    {7,7},
    {8,8},
    {9,9},
    {10,10},
    {11,11},
    {12,12},
    {13,13},
    {14,14},
    {15,15},
    {16,16},
    {17,17},
    {1000,18},
    {1001,19},
    {1002,20},
    {1004,21},
    {1005,22},
    {1006,23},
    {1007,24},
    {1008,25},
    {2002,26},
    {3000,27},
    {3001,28},
    {3003,29},
    {3004,30},
    {3002,31},
    {4000,32},
    {4001,33},
    {4002,34},
    {4003,35},
    {5000,36},
    {5001,37},
    {6000,38},
    {6001,39},
    {7000,40},
    {10000,41},
    {10001,42},
    {10003,43},
    {10002,44},
    {10004,45},
    {10005,46},
    {10006,47},
    {10007,48},
    {10008,49},
    {10009,50},
    {10010,51},
    {10011,52},
    {10012,53},
    {11002,54},
};
}

void EffectResources::initialize(unsigned recipe){
    for(int action:recipes[recipe]){
        if(action<0){initialize(unsigned(-action-1));continue;}
        const auto id=unsigned(action);if(owners_.contains(id))continue;
        const auto& definition=definitions[id];
        std::shared_ptr<const Bytes> bitmap;
        if(*definition.member){
            // 1c6ad8 compares exactly 16 bytes of the archive directory name.
            const auto* files=resources_.first_loaded_files(std::string(definition.member).substr(0,16));
            if(!files)return; // The original initializer returns without acquiring a handle.
            if(files->size()!=1)throw std::runtime_error("Effect texture has multiple installed representations: "+std::string(definition.member));
            const auto& file=files->front();
            const auto expected=definition.pixel_format==19?"effect-index-bitmap-v1":definition.pixel_format==0?"rgba-bitmap-v1":"effect-bitmap-v1";
            if(file.conversion!=expected || !file.bytes)throw std::runtime_error("Effect texture requires installed bitmap conversion: "+std::string(definition.member));
            bitmap=file.bytes;
            const auto u32=[&](unsigned offset){return std::uint32_t((*bitmap)[offset])|(std::uint32_t((*bitmap)[offset+1])<<8)|(std::uint32_t((*bitmap)[offset+2])<<16)|(std::uint32_t((*bitmap)[offset+3])<<24);};
            if(bitmap->size()<54 || (*bitmap)[0]!='B' || (*bitmap)[1]!='M' || u32(18)!=definition.width || u32(22)!=definition.height || (*bitmap)[28]!=(definition.pixel_format==19?8:32))
                throw std::runtime_error("Installed effect bitmap dimensions or format differ: "+std::string(definition.member));
            const auto stride=definition.width*(definition.pixel_format==19?1u:4u);
            if(u32(2)!=bitmap->size() || u32(10)<54 || std::uint64_t(u32(10))+stride*definition.height>bitmap->size())
                throw std::runtime_error("Installed effect bitmap pixel data is truncated: "+std::string(definition.member));
        }
        // Texture slots share source identity and usage; static class owners do not.
        std::shared_ptr<const Texture> texture;
        for(const auto& [owner,existing]:owners_)if(existing->bitmap==bitmap && existing->usage==definition.usage && existing->palette==definition.palette){texture=existing;break;}
        if(!texture)texture=std::make_shared<Texture>(Texture{definition.member,definition.width,definition.height,definition.pixel_format,definition.usage,definition.palette,bitmap});
        owners_.emplace(id,std::move(texture));
    }
}
void EffectResources::release(unsigned recipe){
    for(int action:recipes[recipe])if(action<0)release(unsigned(-action-1));else owners_.erase(unsigned(action));
}
void EffectResources::load(int type){if(const auto it=types.find(type);it!=types.end())initialize(it->second);}
void EffectResources::unload(int type){
    // 193cf0 is empty: type 4000 borrows smoke2 but does not release it.
    if(type==4000)return;
    if(const auto it=types.find(type);it!=types.end())release(it->second);
}
std::vector<std::shared_ptr<const EffectResources::Texture>> EffectResources::textures(int type)const{
    std::vector<std::shared_ptr<const Texture>> result;
    const std::function<void(unsigned)> visit=[&](unsigned recipe){for(int action:recipes[recipe]){
        if(action<0)visit(unsigned(-action-1));else if(const auto it=owners_.find(unsigned(action));it!=owners_.end())result.push_back(it->second);
    }};
    if(const auto it=types.find(type);it!=types.end())visit(it->second);return result;
}
Bindings EffectResources::bindings(){
    Bindings result;
    for(unsigned opcode:{1006u,1007u})result.emplace(opcode,Binding{1,0,[this,opcode](const Invocation& call){
        const double value=std::trunc(call.arguments[0]);
        if(!std::isfinite(value)||value<INT_MIN||value>INT_MAX)throw std::runtime_error("Invalid effect resource type");
        if(opcode==1006)load(int(value));else unload(int(value));return Reply{false,0.f};
    }});
    result.emplace(1008,Binding{0,0,[this](const Invocation&){clear();return Reply{false,0.f};}});
    return result;
}
}
