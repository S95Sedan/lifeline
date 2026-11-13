#include "ui/control_panel.hpp"
#include "script/memory.hpp"
#include <cmath>

namespace lifeline {
namespace {
int integer(float v){if(!std::isfinite(v)||double(v)<INT32_MIN||double(v)>INT32_MAX)throw std::runtime_error("Invalid control panel argument");return int(v);}
constexpr std::array<char,16> keys{'7','8','9',8,'4','5','6',8,'1','2','3',10,'0','*','#',10};
constexpr std::array<std::array<float,2>,16> points{{{53,68},{112,68},{171,68},{233,73},{53,81},{112,81},{171,81},{233,73},{53,94},{112,94},{171,94},{233,98},{53,107},{112,107},{171,107},{233,98}}};
}
void ControlPanel::reset(){panel_.reset();++generation_;queue_.clear();type_=state_=x_=y_=0;active_=standalone_=false;}
void ControlPanel::initialize(int type){
    if(type==4){panel_.reset();++generation_;complete_(false);state_=2;return;}
    if(type<1||type>5)throw std::runtime_error("Control panel type not connected: "+std::to_string(type));
    const auto name="cp0"+std::to_string(type)+".cf";
    auto next=std::make_unique<Panel>(resources_.read_raw("sns/ac.a",name),name);
    panel_=std::move(next);++generation_;selection_=0;text_.clear();
    if(type==3){text_.assign(11,' ');panel_->player.set_text("text_C0","  ",19,20,{.02734375f,.796875f,.796875f,.5f});panel_->player.text_property("text_C0",3,0);}
    if(type!=5)message(34002,2,1);
}
int ControlPanel::message(int code,int first,int second){
    if(code==34003){reset();return 1;}
    if(code<34000||code>34006)throw std::runtime_error("Control panel protocol not connected: "+std::to_string(code));
    if(queue_.size()==16)return 0;queue_.push_back({code,first,second});return 1;
}
void ControlPanel::finish(bool accepted){
    if(standalone_){modes_.state(0);modes_.change(0);}
    complete_(accepted);state_=2;
    if(worker_ && address_)callback_(unsigned(worker_),unsigned(address_));
}
void ControlPanel::input(int action,int quiet){
    if(!panel_)return;
    if(type_==1||type_==2){
        if(action==2||action==3){if(action==3||!quiet)sound_(0);selection_=action==2?1:0;panel_->player.pointer(selection_?80.f:200.f,80.f,false);}
        else if(action==5||action==6){sound_(action==6?1:2);finish(action==6);}
        return;
    }
    if(type_!=3)return;
    int row=selection_/4,col=selection_%4;
    if(action>=0 && action<=3){
        if(action!=2||!quiet)sound_(0);
        if(action==2)col=std::max(0,col-1);
        if(action==3)col=std::min(3,col+1);
        if(action==0 && row>0){if(col!=3)--row;else if(row>=2)row=0;}
        if(action==1 && row<3){if(col!=3)++row;else if(row<=1)row=2;}
        selection_=row*4+col;const auto& p=points[selection_];panel_->player.pointer(p[0],p[1],false);
    }else if(action==6){
        if(col==3 && row>=2){
            sound_(1);
            // Completion is delivered before trimming, as in 148d94..148df0.
            if(standalone_){modes_.state(0);modes_.change(0);}complete_(true);
            const auto first=text_.find_first_not_of(' ');if(first!=std::string::npos)text_.erase(0,first);
            state_=2;if(worker_&&address_)callback_(unsigned(worker_),unsigned(address_));
        }else {
            sound_(0);
            if(col==3){text_.insert(text_.begin(),' ');text_.resize(11);}
            else if(text_.size()==11 && text_.front()==' '){text_.erase(text_.begin());text_+=keys[selection_];}
            panel_->player.text_content("text_C0",text_);
        }
    }else if(action==5){sound_(2);complete_(false);}
}
bool ControlPanel::advance(){
    while(!queue_.empty()){
        const auto [code,first,second]=queue_.front();queue_.pop_front();
        switch(code){
        case 34000:initialize(first);type_=first;state_=1;worker_=address_=0;standalone_=second!=0;active_=true;if(standalone_){notify_(34007,2,1);notify_(34008,3,0);}break;
        case 34001:reset();worker_=address_=0;break;
        case 34002:if(!(first==5&&standalone_))input(first,second);break;
        case 34004:break; // Original jump table deliberately ignores this message.
        case 34005:x_=first;y_=second;break;
        case 34006:worker_=first;address_=second;break;
        }
    }
    if(panel_){
        panel_->player.advance();
        // Retain the native CF pointer through the opening animation and
        // subsequent placements, so the selected button's child keeps pulsing.
        if(type_==1||type_==2)panel_->player.pointer(selection_?80.f:200.f,80.f,false);
        else if(type_==3){const auto& p=points[selection_];panel_->player.pointer(p[0],p[1],false);}
    }
    return active_;
}
std::vector<FlashDraw> ControlPanel::compose()const{
    if(!panel_)return {};auto draws=panel_->player.compose();
    if(type_==5){
        // 148f00 reads a signed PCM sample, normalizes its magnitude by
        // 32768 and draws a 120 x 5 maximum bar at control offset (32,79).
        const float right=32+120*std::abs(int(microphone_sample_))/32768.f;
        FlashDraw meter;meter.indices={0,1,2,2,1,3};const math::Vector4 color{.02734375f,.796875f,.796875f,.5f};
        meter.vertices={{32,79,0,0,color},{right,79,0,0,color},{32,84,0,0,color},{right,84,0,0,color}};draws.push_back(std::move(meter));
    }
    for(auto& d:draws)for(auto& v:d.vertices){v.x+=float(x_);v.y+=float(y_);}return draws;
}
script::Bindings ControlPanel::bindings(){
    using namespace script;Bindings b;
    b.emplace(1044,Binding{3,0,[this](const Invocation& c){const int code=integer(c.arguments[0]),first=integer(c.arguments[1]),second=integer(c.arguments[2]);if(code==34000){modes_.change(2,first,1);return Reply{false,0.f};}return Reply{false,float(message(code,first,second))};}});
    b.emplace(1046,Binding{0,0,[this](const Invocation&){return Reply{false,float(selection_)};}});
    b.emplace(1047,Binding{1,0,[this](const Invocation& c){write_script_string(c,c.arguments[0],text_);return Reply{false,float(selection_)};}});
    return b;
}
}
