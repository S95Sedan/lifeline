#include "script/battle_targeting.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lifeline::script {
std::optional<BattleTargetHelper> identify_battle_target_helper(std::span<const std::uint32_t> words,unsigned entry){
    struct Shape {unsigned length,table_word;std::uint64_t hash;};
    // Complete function fingerprints: relative branches and normalized enemy
    // table relocation. No room-specific addresses or blanket angle rewrite.
    constexpr Shape shapes[]{
        {113,19,0xf0d8af4c87d2c1b3ull}, // eligible(enemy)
        {161,44,0x5469196396bf1451ull}, // closest bearing
        {172,55,0xd1899770ca8dfdf3ull}, // closest bearing excluding enemy
        {218,36,0xc9f02bf9b591082aull}, // closer enemy excluding enemy
    };
    for(unsigned kind=0;kind<std::size(shapes);++kind){
        const auto& shape=shapes[kind];if(entry+shape.length>words.size())continue;
        if(kind==1){if(words[entry]!=32||words[entry+1]!=65536||words[entry+2]!=32||words[entry+3]!=65537)continue;}
        else if(words[entry]!=33||words[entry+1]!=1||words[entry+2]!=65536||words[entry+3]!=32)continue;
        const auto table=words[entry+shape.table_word];
        if(table>=words.size()||words.size()-table<3*36)continue;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned i=0;i<shape.length;++i){
            const auto word=words[entry+i];
            const auto normalized=word==table?0xfffffffeu:word>=entry&&word<entry+shape.length?0x80000000u+word-entry:word;
            hash=(hash^normalized)*1099511628211ull;
        }
        if(hash==shape.hash)return BattleTargetHelper{kind,table};
    }
    return {};
}
float battle_target_helper(const BattleTargetHelper& helper,const Program& program,const State& state,std::span<const float> arguments){
    const auto read=[&](unsigned enemy,unsigned field){
        const auto address=helper.table+enemy*36+field;
        if(!state.data_contains(program,address))throw std::runtime_error("Battle enemy table outside script state");
        const auto found=state.data->find(address);
        return found==state.data->end()?std::bit_cast<float>(program.word(address)):found->second;
    };
    int excluded=-1;
    if(helper.kind!=1){
        if(arguments.size()!=1||!std::isfinite(arguments[0]))throw std::runtime_error("Invalid battle target parameter");
        if(arguments[0]<0||arguments[0]>=3){
            if(helper.kind==0)return 0;
            if(helper.kind==3)return -1;
        }else
        excluded=int(arguments[0]);
    }
    const auto eligible=[&](unsigned enemy){const float value=read(enemy,3);return value!=-1&&value!=-2&&value!=-3&&value!=-4;};
    if(helper.kind==0)return eligible(unsigned(excluded))?1.f:0.f;
    int selected=-1;float best=std::numeric_limits<float>::infinity();
    for(unsigned enemy=0;enemy<3;++enemy){
        if(int(enemy)==excluded||!eligible(enemy))continue;
        if(helper.kind==3&&!(read(enemy,10)<read(unsigned(excluded),10)))continue;
        const float bearing=std::abs(read(enemy,11));
        // Facing ranks candidates but no longer invalidates a living enemy.
        // Include exactly pi (directly behind), unlike the original cone.
        if(std::isfinite(bearing)&&bearing<best){selected=int(enemy);best=bearing;}
    }
    return float(selected);
}
}
