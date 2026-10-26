#include "ui/inventory_panel.hpp"
#include <algorithm>
#include <cmath>

namespace lifeline {
namespace {constexpr math::Vector4 text_color{1,1,1,1};}
void InventoryPanel::initialize(){
    const std::array<std::string,3> names{keywords_?"kw_sel.cf":"item_cs.cf",!keywords_&&category_==1?"item_open4.cf":"item_kl.cf","item_text.cf"};
    decltype(panels_) next;
    for(unsigned i=0;i<next.size();++i)next[i]=std::make_unique<Panel>(resources_.read_raw("sns/ac.a",names[i]),names[i]);
    panels_=std::move(next);++generation_;row_=0;scroll_=-1;
    // 160f08 registers event 3: the scrolling CF reports g_nAnimationEnd,
    // not a button-id event. Commit the new window after its animation.
    panels_[1]->player.on_event([this](unsigned kind,int value){if(kind==3)message(1,0,value);});
    const float x=keywords_?std::array<float,4>{32,64,112,160}.at(unsigned(category_)):category_?132.f:32.f;
    panels_[0]->player.pointer(x,keywords_?96.f:94.f,false);
    populate();
}
int InventoryPanel::selected()const{
    if(entries_.empty())return -1;
    if(entries_.size()<7||(!keywords_&&category_==1))return entries_.at(std::min<unsigned>(unsigned(row_),unsigned(entries_.size()-1)));
    const int index=(scroll_+row_+1)%int(entries_.size());return entries_.at(unsigned(index<0?index+int(entries_.size()):index));
}
void InventoryPanel::populate(){
    entries_.clear();
    for(const auto& [id,r]:inventory_.records(keywords_?1:0)){
        const int flag=r.fields[1];const bool owned=flag>=0&&flag<32768&&flags_[unsigned(flag)];
        if(keywords_){if(r.fields[3]==category_&&(flag==-1||owned))entries_.push_back(id);}
        else if(category_==0){if(r.fields[3]==1&&owned)entries_.push_back(id);}
        else if(r.fields[3]==3)entries_.push_back(id);
    }
    if(!panels_[1])return;
    row_=std::clamp(row_,0,entries_.empty()?0:std::min(5,int(entries_.size()-1)));
    if(entries_.size()<7)scroll_=0;
    if(!keywords_&&category_==1){
        for(unsigned i=0;i<3;++i){const auto* r=inventory_.record(0,std::array<int,3>{3,4,7}[i]);const bool owned=r&&r->fields[1]>=0&&flags_[unsigned(r->fields[1])];const auto prefix="text_0"+std::to_string(i+1);
            panels_[1]->player.set_text(prefix+"_01",owned?r->label:"",40,20,text_color);
            const int quantity=owned?std::clamp(r->quantity(),0,99):0;
            auto count=owned?std::to_string(quantity):"";
            if(owned&&quantity<10)count="0"+count;
            panels_[1]->player.set_text(prefix+"_02",count,12,20,text_color);
            panels_[1]->player.text_property(prefix+"_02",3,0);
            // 161828..161c10: authored quantity glyphs in the adjacent box.
            // Capsules cap at 10, grenades at 8; chips use groups of
            // four plus a remainder glyph, up to sixteen represented units.
            std::string symbols;
            const auto append=[&](unsigned code){symbols+=char(code>>8);symbols+=char(code&255);};
            if(i<2){for(int n=0;n<std::min(quantity,i==0?10:8);++n)append(i==0?0x81aa:0x81a9);}
            else {const int units=std::min(quantity,16);for(int n=0;n<units/4;++n)append(0x8743);if(units%4)append(0x8740+units%4-1);}
            panels_[1]->player.set_text(prefix+"_03",symbols,i==2?50.f:20.f,i==2?50.f:20.f,{.02734375f,.796875f,.796875f,.5f});
            panels_[1]->player.text_property(prefix+"_03",3,0);
        }
    }else{
        std::string list=entries_.size()<7?"@pv50\n":"";
        const unsigned count=std::min<unsigned>(8,unsigned(entries_.size()));
        for(unsigned i=0;i<count;++i){int index=entries_.size()<7?int(i):(scroll_+int(i))%int(entries_.size());if(index<0)index+=int(entries_.size());list+=inventory_.record(keywords_?1:0,entries_[unsigned(index)])->label+'\n';}
        panels_[1]->player.set_text("text_KW0",list,40,20,text_color);panels_[1]->player.text_property("text_KW0",3,1);
    }
    const int id=selected();const auto* selected_record=id<0?nullptr:inventory_.record(keywords_?1:0,id);
    panels_[2]->player.set_text("text_02",selected_record?selected_record->description:"",36,20,text_color);
    panels_[2]->player.pointer(455,5,false);
    panels_[1]->player.pointer(!keywords_&&category_==1?25.f:32.f,(!keywords_&&category_==1?170.f:144.f)+row_*(!keywords_&&category_==1?79.f:50.f),false);
}
void InventoryPanel::input(int action){
    if(!active_||closing_)return;
    if(action<0||action>3)return;
    sound_(0);
    if(action==2||action==3){const int next=std::clamp(category_+(action==2?-1:1),0,keywords_?3:1);if(next!=category_){category_=next;initialize();}return;}
    const int next=row_+(action==0?-1:1);
    if(!keywords_&&category_==1){
        const std::array<bool,3> allowed{true,flags_[682],flags_[200]};
        for(int row=next;row>=0&&row<3;row+=action==0?-1:1)if(allowed[unsigned(row)]){row_=row;break;}
    }else if(next>=0&&next<std::min(6,int(entries_.size())))row_=next;
    else if(entries_.size()>=7){panels_[1]->player.pointer(32,action==0?144.f:394.f,true);return;}
    populate();
}
int InventoryPanel::message(int code,int first,int second){
    switch(code){
    case 34000:category_=0;initialize();active_=true;closing_=false;transition_=0;scene_scale_=1;break;
    case 34001:release_flash();closing_=true;transition_=0;break;
    case 34003:release_flash();active_=closing_=false;transition_=-1;category_=row_=0;scroll_=-1;scene_scale_=1;entries_.clear();break;
    case 34002:input(first);break;
    case 34005:notify_(code,first,second);break;
    case 1:if(keywords_||category_==0){if(second==1)--scroll_;else if(second==0)++scroll_;populate();}break;
    default:break; // The original dispatchers ignore unmatched messages.
    }return 1;
}
bool InventoryPanel::advance(){
    // 1627a0 / 162850: eleven steps of 0.03, anchored to the top right
    // by camera scaling mode 2 (1ca57c). The last open scale is 0.7.
    if(transition_>=0&&transition_<11)scene_scale_=closing_?.7f+.03f*transition_:1.f-.03f*transition_;
    if(transition_>=0){if(transition_>=11){transition_=-1;if(closing_)active_=false;else notify_(34007,int(mode()),1);}else ++transition_;}
    if(active_&&!closing_){
        for(auto& panel:panels_)if(panel)panel->player.advance();
        // Reapply hover after timeline updates replace button placements,
        // just as the shared CF service updates its retained pointer.
        panels_[0]->player.pointer(keywords_?std::array<float,4>{32,64,112,160}.at(unsigned(category_)):category_?132.f:32.f,keywords_?96.f:94.f,false);
        populate();
    }
    return active_;
}
std::vector<InventoryPanel::Draw> InventoryPanel::compose()const{
    std::vector<Draw> out;
    // The list uses draw band 8 (160e80); the header and description retain
    // the CF default band 10. Its black backing must precede the header.
    for(unsigned i:std::array<unsigned,3>{1,0,2})if(panels_[i])for(auto draw:panels_[i]->player.compose()){
        if(i==2)for(auto& v:draw.vertices){v.x+=192;v.y+=314;}
        out.push_back({i,std::move(draw)});
    }return out;
}
script::Bindings InventoryPanel::bindings(){
    if(keywords_)return {};
    return {{1091,script::Binding{3,0,[this](const script::Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>=2147483648.)throw std::runtime_error("Invalid inventory display message");
        return script::Reply{false,float(message(int(c.arguments[0]),int(c.arguments[1]),int(c.arguments[2])))};
    }}}};
}
}
