#include "ui/title.hpp"
#include <bit>

namespace lifeline {
TitleNavigation::TitleNavigation(std::span<const unsigned char> script) {
    // Version-specific data extraction, not a script interpreter. Strings and
    // size identify the recovered layout; the coordinate tables remain in ISO.
    if(script.size()!=504948 || std::string_view(reinterpret_cast<const char*>(script.data()+0x4d5c),11)!="tit_load.cf" ||
        std::string_view(reinterpret_cast<const char*>(script.data()+0x4d88),14)!="tit_loadcur.cf")
        throw std::runtime_error("Unsupported title navigation script");
    const auto read=[&](unsigned word,std::array<TitlePoint,4>& points) {
        for(unsigned i=0;i<4;++i) {
            auto& p=points[i];p.x=std::bit_cast<float>(le32(script.data()+word*4+i*8));
            p.y=std::bit_cast<float>(le32(script.data()+word*4+i*8+4));
            if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.x>640 || p.y<0 || p.y>448)
                throw std::runtime_error("Invalid title navigation coordinates");
        }
    };
    read(5181,foreground);read(5193,up);read(5201,down);read(5209,confirm);
}
const char* TitleController::label(unsigned selection) {
    static constexpr const char* labels[]={"New Game","Load Game","Option","Bonus"};
    if(selection>=4) throw std::runtime_error("Invalid title selection");return labels[selection];
}
TitleController::TitleController(FlashPlayer& foreground,const FlashMovie& cursor,const TitleNavigation& navigation)
    :foreground_(foreground),cursor_(cursor),navigation_(navigation) {
    if(foreground.movie().frames.size()!=71 || cursor.frames.size()!=360)
        throw std::runtime_error("Unsupported title movie layout");
    if(foreground.frame()>=61) {
        selection_=std::min((foreground.frame()-61)/3,3u);begin_menu();
        cursor_.seek(selection_*60);
    } else if(foreground.frame()>3) phase_=Phase::reveal;
}
void TitleController::begin_menu() {
    cursor_.seek(0);phase_=Phase::menu;
}
void TitleController::start() {
    if(phase_!=Phase::start || foreground_.frame()!=3) return;
    idle_=0;foreground_.click(320,278);phase_=Phase::reveal;
}
void TitleController::move(int direction) {
    if(direction!=-1 && direction!=1) throw std::runtime_error("Invalid title direction");
    if(phase_==Phase::menu) idle_=0;
    if(phase_!=Phase::menu || (direction<0 && selection_==0) || (direction>0 && selection_==3)) return;
    selection_=static_cast<unsigned>(static_cast<int>(selection_)+direction);
    const auto point=navigation_.foreground[selection_];foreground_.click(point.x,point.y);
    cursor_target_=(direction<0?navigation_.up:navigation_.down)[selection_];
    cursor_.click(cursor_target_.x,cursor_target_.y);
    settle_=3;phase_=Phase::moving;
}
void TitleController::confirm() {
    if(phase_!=Phase::menu) return;
    idle_=0;const auto point=navigation_.confirm[selection_];cursor_.click(point.x,point.y);
    phase_=Phase::confirming;
}
void TitleController::back() {
    if(phase_!=Phase::menu&&phase_!=Phase::moving&&phase_!=Phase::reveal)return;
    foreground_.seek(3);cursor_.seek(0);
    phase_=Phase::start;request_=Request::none;selection_=settle_=idle_=0;
}
void TitleController::advance() {
    // entry_1004.cb: 0x0ee0 Start wait = 1800 yields; 0x20f0 menu
    // tests counter >= 9800 before incrementing it (9801 idle iterations).
    if((phase_==Phase::start && foreground_.frame()==3) || phase_==Phase::menu) {
        const auto limit=phase_==Phase::start?1800u:9801u;
        if(++idle_>=limit) {request_=Request::attract;phase_=Phase::unavailable;return;}
    }
    foreground_.advance();
    if(phase_==Phase::reveal && foreground_.frame()==61 && !foreground_.playing()) begin_menu();
    else if(cursor_visible()) cursor_.advance();
    if(phase_==Phase::moving) {
        // The script repeats its virtual cursor click over successive yields.
        if(settle_) { --settle_;cursor_.click(cursor_target_.x,cursor_target_.y); }
        if(!settle_ && !foreground_.playing()) phase_=Phase::menu;
    }
    if(phase_==Phase::confirming && !cursor_.playing()) {
        request_=static_cast<Request>(selection_+1);phase_=Phase::unavailable;
    }
}
}
