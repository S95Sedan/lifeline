#include "script/program.hpp"
#include "script/battle_targeting.hpp"
#include "script/save_menu.hpp"
#include "script/movie_control.hpp"
#include <algorithm>
#include <stdexcept>

namespace lifeline::script {
Program::Program(std::string name,std::span<const std::uint8_t> bytes):name_(std::move(name)) {
    if(bytes.empty() || bytes.size()%4)throw std::runtime_error(name_+": invalid script size");
    words_.reserve(bytes.size()/4);
    for(std::size_t p=0;p<bytes.size();p+=4)words_.push_back(std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|(std::uint32_t(bytes[p+2])<<16)|(std::uint32_t(bytes[p+3])<<24));
    if(name_=="entry_1004.cb"){
        // EOF and player interruption have different title destinations. Reuse
        // each existing cleanup/return path, returning the host skip reason.
        for(unsigned byte:{0x25e0u,0x2780u}){
            const auto at=byte/4,delta=(byte-0x25e0)/4;
            const auto pad=0x2588/4+delta,skipped=0x25c4/4+delta;
            if(at+3<words_.size()&&words_[0x252c/4+delta]==808&&words_[0x2554/4+delta]==at
                &&words_[pad]==123&&words_[pad+1]==0&&words_[pad+2]==11&&words_[pad+3]==1
                &&words_[skipped]==887&&words_[skipped+1]==888&&words_[skipped+2]==1&&words_[skipped+3]==5
                &&words_[at]==887&&words_[at+1]==888&&words_[at+2]==0&&words_[at+3]==5)
                words_[at+1]=movie_skip_result_opcode;
        }
    }
    // entry_013's opening waits for picture 1, then counts 20800 ticks without
    // checking movie completion (dev/opening-movie-script.txt, f6248..f63b0).
    // Check state 815 inside that loop so EOF/player skip reaches the authored
    // fade and stop. Keep the countdown: after_movie uses capture playback,
    // which never reaches EOF and still needs this original exit to clean up.
    if(name_=="entry_013.cb"){
        constexpr std::uint32_t ready[]{887,893,260420,901,808,887,890,0,888,1,900,895,11,0,252083,887,890,2,812,895,887,890,2,888,1,908,34,252080,1,252083,25,1,252062};
        constexpr std::uint32_t countdown[]{887,890,0,888,20800,895,11,0,252109,123,0,8,3,887,890,3,888,0,905,34,252106,1,252109,25,1,252089};
        constexpr std::uint32_t cleanup[]{887,889,0,901,888,0,901,888,0,901,888,640,901,888,448,901,889,1132396544,901,889,1132396544,901,889,1132396544,901,888,1,901,167,887,809,25};
        const auto matches=[&]<std::size_t N>(unsigned byte,const std::uint32_t (&expected)[N]){
            const auto at=byte/4;return at+N<=words_.size()&&std::equal(std::begin(expected),std::end(expected),words_.begin()+at);
        };
        if(matches(0xf6248,ready)&&matches(0xf62cc,countdown)&&matches(0xf6334,cleanup)){
            // BEGIN; state; branch-if-zero cleanup; original pad query;
            // LOCAL 3. Its following branch already tests zero, so the old
            // redundant "!= 0" comparison makes room without moving targets.
            constexpr std::uint32_t completion[]{887,815,34,0xf6334/4,123,0,8,3,890,3};
            std::copy(std::begin(completion),std::end(completion),words_.begin()+0xf62f0/4);
        }
    }
    // Initialize the shared battle camera even if target selection has already
    // completed. Otherwise the initial -1 camera state can retain a cinematic
    // eye until the first attack changes the actor mode. Keep the outer -1
    // guard and execute the authored camera/table setup without its inner
    // "no target or busy" condition. Assets and established cameras are intact.
    if(name_.ends_with(".cb"))for(std::size_t i=0;i+42<words_.size();++i){
        constexpr std::uint32_t guard[]{887,890,2,888,1,900,904,34};
        constexpr std::uint32_t selected[]{887,890,10,888,1,900,904,888};
        constexpr std::uint32_t busy[]{901,888,3,901,1085,888,0,905,903,34};
        constexpr std::uint32_t setup[]{887,888,6,901,545,887,890,2,888,2,900,895};
        if(std::equal(std::begin(guard),std::end(guard),words_.begin()+i)&&
           std::equal(std::begin(selected),std::end(selected),words_.begin()+i+9)&&
           std::equal(std::begin(busy),std::end(busy),words_.begin()+i+18)&&
           std::equal(std::begin(setup),std::end(setup),words_.begin()+i+29)&&
           words_[i+8]==words_[i+28]&&words_[i+8]>i+40&&words_[i+8]<words_.size()&&words_[i+17]<words_.size()-4){
            words_[i+9]=1;words_[i+10]=unsigned(i+29);
        }
    }
    // PC storage presentation: keep all I/O, result checks and confirmations,
    // but not the console's fixed memory-card dwell times. These programs contain
    // the same shared storage helpers. Verify each instruction before adapting
    // its duration; installed scripts and serialized saves remain unchanged.
    if(has_native_save_menu(name_)){
        // Bonus loads inside the title program, which embeds another copy of
        // these same storage workers instead of entering entry_101.
        const int offset=name_=="entry_1004.cb"?-0xb4b8:name_=="entry_101.cb"?0x640:0;
        // The file panel already plays the opening cue (5b254). Its controls
        // hint, inputinfo.cf, independently repeats the same cue at 5a710.
        // Keep one opening sound; bypass only the hint's duplicate statement.
        const auto hint=(0x5a6e8-offset)/4;
        constexpr std::uint32_t cue[]{887,888,0,901,888,20,901,888,3,901,702};
        bool matches=hint+11<words_.size();
        for(unsigned i=0;matches&&i<11;++i)matches=words_[hint+i]==cue[i];
        if(matches){words_[hint]=1;words_[hint+1]=hint+11;}
        // Bypass the card-check presentation, not the following disk checks.
        // Each range only sends open/text messages and waits for presentation.
        for(const auto [begin,end]:{std::pair{0x571bcu,0x57240u},std::pair{0x5d34cu,0x5d3d0u},std::pair{0x5e110u,0x5e194u}}){
            const auto b=(begin-offset)/4,e=(end-offset)/4;
            if(e<words_.size()&&words_[b]==887&&words_[e]==887&&words_[e-2]==12){words_[b]=1;words_[b+1]=e;}
        }
        // Matching close messages for the omitted checking window.
        for(const auto [begin,end]:{std::pair{0x577dcu,0x57814u},std::pair{0x5d3f8u,0x5d430u},std::pair{0x5e25cu,0x5e294u}}){
            const auto b=(begin-offset)/4,e=(end-offset)/4;
            if(e<words_.size()&&words_[b]==887&&words_[b+4]==888&&words_[b+5]==104&&words_[e-1]==145){words_[b]=1;words_[b+1]=e;}
        }
        // After the successful write and acknowledgement, refresh the current
        // slot directly. Do not close both panels and send event 1008 through
        // the card/menu reinitialization path before returning to the selector.
        const auto success=(0x592d4-offset)/4;
        if(success+13<words_.size()&&words_[success]==887&&words_[success+1]==890&&words_[success+2]==5&&words_[success+5]==104&&words_[success+13]==145){
            const std::uint32_t resume[]{887,890,10,888,0,895,1,unsigned((0x57ac0-offset)/4)};
            for(unsigned i=0;i<8;++i)words_[success+i]=resume[i];
        }
        for(const unsigned byte:{0x5722cu,0x58f50u,0x5d3bcu,0x5e180u,0x5f888u}){
            const auto p=(byte-offset)/4;
            if(p+4<words_.size()&&words_[p]==35&&words_[p+2]==60&&words_[p+3]==12&&words_[p+1]==words_[p+4])words_[p+2]=0;
        }
        // Successful-save acknowledgement: 30 ticks instead of 5*60.
        // Failure dialogs retain their full duration and interaction.
        const auto p=(0x592ac-offset)/4;
        if(p+5<words_.size()&&words_[p]==888&&words_[p+1]==5&&words_[p+2]==888&&words_[p+3]==60&&words_[p+4]==898&&words_[p+5]==901){words_[p+1]=1;words_[p+3]=30;}
        adapt_save_menu(words_,offset);
    }
    if(name_.ends_with(".cb"))for(unsigned entry=0;entry<words_.size();++entry){
        if(words_[entry]!=32&&words_[entry]!=33)continue;
        if(const auto helper=identify_battle_target_helper(words_,entry))battle_helpers_.emplace(entry,std::pair{helper->kind,helper->table});
    }
}
std::uint32_t Program::word(std::uint32_t address) const {
    if(address>=words_.size())throw std::out_of_range(name_+": script address outside resource");
    return words_[address];
}
std::string Program::string(std::uint32_t address) const {
    std::string result;
    for(auto i=address;i<words_.size();++i){const auto value=word(i);for(unsigned j=0;j<4;++j){const char c=char(value>>(j*8));if(!c)return result;result+=c;}}
    throw std::runtime_error(name_+": unterminated script string");
}
EventTable::EventTable(const Program& program,std::span<const std::uint8_t> bytes){
    // One leading entry (nonzero in cs1002), then 16384 event addresses.
    if(bytes.size()!=65540)throw std::runtime_error("Unsupported event table size");
    Program table(program.name()+" event table",bytes);
    leading_entry_=table.word(0);
    if(leading_entry_>=program.size())throw std::runtime_error("Leading entry outside script");
    for(unsigned i=1;i<table.size();++i){const auto address=table.word(i);if(address>=program.size())throw std::runtime_error("Event entry outside script");entries_.push_back(address);}
}
std::optional<std::uint32_t> EventTable::entry(unsigned event) const {
    if(event>=entries_.size())throw std::out_of_range("Event ID outside table");
    return entries_[event]?std::optional<std::uint32_t>(entries_[event]):std::nullopt;
}
}
