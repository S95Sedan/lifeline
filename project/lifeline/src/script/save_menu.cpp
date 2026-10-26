#include "script/save_menu.hpp"
#include <bit>
#include <string>
#include <stdexcept>
#include <algorithm>

namespace lifeline::script {
void adapt_save_menu(std::vector<std::uint32_t>& w,int offset){
    const auto original_size=w.size();
    const auto at=[&](unsigned byte){return unsigned((std::int64_t(byte)-offset)/4);};
    const auto replace=[&](unsigned byte,unsigned before,unsigned after){
        auto& value=w.at(at(byte));
        if(value!=before)throw std::runtime_error("Unexpected storage menu instruction");
        value=after;
    };
    const auto append=[&](const std::vector<std::uint32_t>& code){auto start=unsigned(w.size());w.insert(w.end(),code.begin(),code.end());return start;};
    const auto redirect=[&](unsigned begin,unsigned end,std::vector<std::uint32_t> code){
        code.insert(code.end(),{1,at(end)});const auto start=append(code);
        w.at(at(begin))=1;w.at(at(begin)+1)=start;
    };
    const auto string=[&](std::string value){
        const auto start=unsigned(w.size());value.push_back('\0');while(value.size()%4)value.push_back('\0');
        for(unsigned i=0;i<value.size();i+=4){std::uint32_t v=0;for(unsigned j=0;j<4;++j)v|=std::uint32_t(static_cast<unsigned char>(value[i+j]))<<(8*j);w.push_back(v);}return start;
    };
    // The successful disk-read branch used a second acknowledgement dialog
    // with a fixed five-second dwell. Continue straight to the existing panel
    // close/handoff after success instead. Do not bypass the read, its result
    // check, confirmation, success sound, or the separate failure dialog.
    if(w.at(at(0x5f93c))!=887||w.at(at(0x5f984))!=2||w.at(at(0x5f984)+1)!=at(0x55920))
        throw std::runtime_error("Unexpected load acknowledgement helper");
    redirect(0x5f93c,0x5f994,{});
    replace(0x5f900,702,load_cue_play_opcode);
    // Close the panels concurrently with the cue, but do not let scene teardown
    // stop it. Wait for this instance only, including its queued output, never
    // for unrelated menu/music loops or a hard-coded number of seconds.
    redirect(0x5fa04,0x5fa1c,{load_cue_wait_opcode,887,890,18,888,1,895});
    // Grow metadata allocations, not the four-row CF layout.
    for(unsigned byte:{0x56f94u,0x5d158u,0x5dee8u})replace(byte,4,save_slot_count);
    replace(0x55f48,4,save_slot_count); // scan every numbered file
    // All reset sites must initialize every record, including empty slots.
    for(unsigned begin:{0x55db0u,0x56fc0u,0x57240u,0x5d184u,0x5df14u,0x5e194u}){
        const auto p=at(begin);const unsigned local=w.at(p+2);
        if(w.at(p)!=887||w.at(p+7)!=2||w.at(p+8)!=at(0x51e68))throw std::runtime_error("Unexpected save metadata initializer");
        std::vector<std::uint32_t> code;
        for(unsigned slot=0;slot<save_slot_count;++slot)code.insert(code.end(),{887,890,local,901,888,slot,901,2,at(0x51e68),2});
        redirect(begin,begin+160,std::move(code));
    }
    // Independent scan/save/load tables retain original resource and file names
    // for slots 1-4; additional slots use the identical serialization path.
    for(const auto [table,columns]:{std::pair{103352u,2u},std::pair{103361u,1u},std::pair{103463u,1u}}){
        const auto old=at(table*4);
        std::vector<std::uint32_t> entries;
        for(unsigned slot=1;slot<=save_slot_count;++slot){
            if(columns==2)entries.push_back(std::bit_cast<std::uint32_t>(float(string("test_"+std::to_string(slot)+".dat"))));
            entries.push_back(std::bit_cast<std::uint32_t>(float(string("BASLUS-20848-"+std::to_string(slot)+".dat"))));
        }
        const auto next=append(entries);unsigned references=0;
        for(unsigned i=0;i+1<original_size;++i)if(w[i]==888&&w[i+1]==old){w[i+1]=next;++references;}
        if(!references)throw std::runtime_error("Missing storage filename table");
    }
    // Authored selection IDs begin at 3. Wrap across the complete list.
    for(unsigned byte:{0x58360u,0x58594u,0x5efccu,0x5f1c8u})replace(byte,6,save_slot_count+2);
    // Save confirmation accepts every valid row, not just the original four.
    redirect(0x580e0,0x58148,{887,890,4,888,3,908,890,4,888,save_slot_count+2,909,902,34,at(0x58194)});
    // Replace the shared panel's selection and metadata presentation with the
    // native scrolling view. I/O, confirmations, errors and scene handoffs stay
    // in their existing authored workers. No hidden second selection exists.
    redirect(0x5b2d0,0x5b490,{887,890,1,901,888,0,901,890,3,901,save_menu_update_opcode});
    redirect(0x5b500,0x5cb08,{887,890,1,901,888,1,901,890,3,901,save_menu_update_opcode});
}
}
