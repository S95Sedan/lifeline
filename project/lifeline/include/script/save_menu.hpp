#pragma once
#include <cstdint>
#include <vector>
#include <string_view>

namespace lifeline::script {
inline constexpr unsigned save_slot_count=12;
inline constexpr unsigned save_visible_rows=4;
// Native UI service, used only by the adapted PC storage menus.
inline constexpr unsigned save_menu_update_opcode=1200;
inline constexpr unsigned load_cue_play_opcode=1201,load_cue_wait_opcode=1202;
inline bool has_native_save_menu(std::string_view name){
    return name=="entry_100.cb"||name=="entry_101.cb"||name=="entry_1004.cb";
}
void adapt_save_menu(std::vector<std::uint32_t>& words,int byte_offset);
struct SaveListWindow {
    unsigned selected=0,first=0;
    void select(unsigned slot){
        if(slot>=save_slot_count)return;
        selected=slot;
        if(selected<first)first=selected;
        if(selected>=first+save_visible_rows)first=selected-save_visible_rows+1;
    }
    unsigned row()const{return selected-first;}
};
}
