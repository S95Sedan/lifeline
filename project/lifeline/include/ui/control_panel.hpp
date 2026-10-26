#pragma once
#include "assets/resources.hpp"
#include "assets/flash.hpp"
#include "script/command_modes.hpp"
#include <deque>

namespace lifeline {
// Service 8095: authored choice and keypad controls, with deferred messages.
class ControlPanel {
public:
    struct Panel {FlashMovie movie;FlashPlayer player;Panel(Bytes bytes,const std::string& name):movie(bytes,name),player(movie){}};
    using Notify=std::function<void(int,int,int)>;
private:
    Resources& resources_;
    script::CommandModes& modes_;
    Notify notify_;
    std::function<void(bool)> complete_;
    std::function<void(unsigned,unsigned)> callback_;
    std::function<void(unsigned)> sound_;
    std::unique_ptr<Panel> panel_;
    std::deque<std::array<int,3>> queue_;
    int type_=0,state_=0,selection_=0,x_=0,y_=0,worker_=0,address_=0;
    bool active_=false,standalone_=false;
    std::string text_;
    std::uint64_t generation_=0;
    std::int16_t microphone_sample_=0;
    void initialize(int);
    void input(int,int);
    void finish(bool);
    void reset();
public:
    ControlPanel(Resources& r,script::CommandModes& m,Notify n,std::function<void(bool)> c,
                 std::function<void(unsigned,unsigned)> cb,std::function<void(unsigned)> sound)
        :resources_(r),modes_(m),notify_(std::move(n)),complete_(std::move(c)),callback_(std::move(cb)),sound_(std::move(sound)){}
    // 147f10 clears the display pointer only; queued messages and selection survive.
    void release_flash(){panel_.reset();++generation_;}
    int message(int,int=0,int=0);
    void microphone_sample(std::int16_t value){microphone_sample_=value;}
    bool advance();
    script::Bindings bindings();
    std::vector<FlashDraw> compose()const;
    const Panel* panel()const{return panel_.get();}
    auto generation()const{return generation_;}
    bool active()const{return active_;}
    // Type 5 is the authored microphone-level display used by the Voice
    // Suite. It overlays the scene while normal speech capture stays active.
    // An inspection control shares the scene's speech worker (e.g. the
    // simultaneous exhibit-door switches). Only modal standalone choices
    // own input exclusively. Script speech-enable gates still apply.
    bool blocks_speech()const{return active_&&standalone_&&type_!=5;}
    int state()const{return state_;}
    int type()const{return type_;}
    int selection()const{return selection_;}
    const std::string& text()const{return text_;}
};
}
