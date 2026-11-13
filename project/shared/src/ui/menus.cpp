#include "ui/menus.hpp"
#include "script/save_menu.hpp"
#include <fstream>

namespace lifeline {
namespace {
void validate_settings(const MenuSettings& value) {
    if(value.controls>2 || value.sound>1 || value.screen_x<-60 || value.screen_x>60 || value.screen_x%3 || value.screen_y<-20 || value.screen_y>20)
        throw std::runtime_error("Invalid native menu settings");
    for(unsigned col=0;col<2;++col){const auto& list=col?value.bindings.joystick:value.bindings.keyboard;for(unsigned i=0;i<list.size();++i){
        if(!valid_binding(list[i],col))throw std::runtime_error("Invalid saved control binding");
        for(unsigned j=0;j<i;++j)if(list[i].kind!=BindingKind::none&&list[i]==list[j])throw std::runtime_error("Duplicate saved control binding");
    }}
}
}
MenuSettings read_menu_settings(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path)) return {};
    std::ifstream input(path);std::string signature;MenuSettings result;
    if(!(input>>signature>>result.controls>>result.sound>>result.screen_x>>result.screen_y) || (signature!="lifeline-options-1"&&signature!="lifeline-options-2"))
        throw std::runtime_error("Cannot read native menu settings: "+path.string());
    if(signature=="lifeline-options-2")for(unsigned col=0;col<2;++col)for(auto& b:col?result.bindings.joystick:result.bindings.keyboard){int kind;if(!(input>>kind>>b.code>>b.direction)||kind<0||kind>int(BindingKind::joystick_hat))throw std::runtime_error("Cannot read saved control binding");b.kind=BindingKind(kind);}
    input>>std::ws;if(!input.eof()) throw std::runtime_error("Unexpected native menu settings data");
    validate_settings(result);return result;
}
void write_menu_settings(const std::filesystem::path& path,const MenuSettings& settings) {
    validate_settings(settings);
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary=path.string()+".tmp";std::ofstream output(temporary,std::ios::trunc);
    output<<"lifeline-options-2\n"<<settings.controls<<' '<<settings.sound<<' '<<settings.screen_x<<' '<<settings.screen_y<<'\n';
    for(unsigned col=0;col<2;++col)for(const auto& b:col?settings.bindings.joystick:settings.bindings.keyboard)output<<int(b.kind)<<' '<<b.code<<' '<<b.direction<<'\n';
    output.close();if(!output) throw std::runtime_error("Cannot save native menu settings: "+path.string());
    std::filesystem::rename(temporary,path);
}
OptionsAssets::OptionsAssets(std::span<const unsigned char> script) {
    // SLUS_208.48 entry_1017.cb: six original help strings and menu identity.
    if(script.size()!=336876 || std::string(reinterpret_cast<const char*>(script.data()+0x51ad0),11)!="op_title.cf")
        throw std::runtime_error("Unsupported Options script");
    constexpr std::array<unsigned,6> offsets{0x51818,0x51868,0x518ac,0x518e0,0x5192c,0x5195c};
    for(unsigned i=0;i<offsets.size();++i) {
        const auto begin=script.begin()+offsets[i],end=std::find(begin,script.end(),0);
        if(end==script.end()) throw std::runtime_error("Unterminated Options help text");
        help[i]=std::string(begin,end);
    }
}
EmptySaveController::EmptySaveController(const SaveMenuAssets& assets,bool bonus)
    :window_(assets.window),title_(assets.title),input_(assets.input),bonus_(bonus) {notice();}
void EmptySaveController::notice() {
    phase_=Phase::notice;yes_=false;
    // Platform wording: this port uses native saves, not PS2 memory cards.
    window_.set_text("text_sys",std::string(script::empty_save_message),24,24);
}
void EmptySaveController::advance() {
    if(phase_==Phase::closed) return;
    window_.advance();title_.advance();input_.advance();
    if(phase_==Phase::returning && !window_.playing()) notice();
    if(phase_==Phase::closing) {
        if(window_.finished()) phase_=Phase::closed;
        else if(!window_.playing() && window_.frame()==19) window_.click(610,190);
    }
    if(phase_==Phase::question && !window_.playing()) window_.pointer(500,yes_?330:360,false);
}
void EmptySaveController::move(int vertical) {
    if(phase_!=Phase::question || window_.playing()) return;
    if(vertical) yes_=vertical<0;
    window_.pointer(500,yes_?330:360,false);
}
void EmptySaveController::confirm() {
    if(window_.playing()) return;
    if(phase_==Phase::notice) {
        window_.set_text("text_sys",bonus_?"\n\n The BONUS mode cannot be played without\n''LIFELINE'' save data.\n\n Cancel BONUS mode?":"\n\n\n Cancel load?",24,24);
        window_.click(610,240);phase_=Phase::question;yes_=false;
    } else if(phase_==Phase::question) {
        if(yes_) {window_.click(500,330);phase_=Phase::closing;}
        else {window_.click(500,360);phase_=Phase::returning;}
    }
}
void EmptySaveController::back() {
    if(window_.playing()) return;
    if(phase_==Phase::notice) confirm();
    else if(phase_==Phase::question) {yes_=false;confirm();}
}

}
