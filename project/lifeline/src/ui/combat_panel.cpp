#include "ui/combat_panel.hpp"

namespace lifeline {
namespace {
constexpr math::Vector4 cyan{.02734375f,.796875f,.796875f,.5f};
std::string digits(int value){value=std::min(value,99);return {char('0'+value/10),char('0'+value%10)};}
}
void CombatPanel::items(){if(panels_[0]){panels_[0]->player.set_text("text_h",digits(quantity_(3)),20,16,cyan);panels_[0]->player.text_property("text_h",3,0);}}
void CombatPanel::ammunition(int count){
    if(!panels_[0])return;
    if(count>31)throw std::runtime_error("Combat ammunition exceeds native text buffer");
    panels_[0]->player.text_content("text_bu0",std::string(unsigned(std::max(count,0)),'!'));
    panels_[0]->player.set_text("text_l",digits(quantity_(4)),20,16,cyan);
    panels_[0]->player.text_property("text_l",3,0);
}
int CombatPanel::message(int message,int first,int second,bool expanded){
    switch(message){
    case 34000:{
        // Build every panel before replacing the current display.
        std::array<std::unique_ptr<Panel>,7> next;
        const std::array<std::string,7> names{expanded?"bat02.cf":"bat01.cf","elg.cf","bat.cf","sl.cf","en.cf","en.cf","en.cf"};
        for(unsigned i=0;i<next.size();++i)next[i]=std::make_unique<Panel>(assets_.read("ui/combat/"+names[i]),names[i]);
        panels_=std::move(next);++generation_;targets_.reset();
        for(int i=0;i<3;++i){std::array<float,14> values{};values[4]=1;targets_.update(i,values);}
        auto& player=panels_[0]->player;
        player.set_text("text_bu0",std::string(expanded?30:15,'!'),10,16,cyan);
        player.text_property("text_bu0",3,0);player.text_property("text_h",3,0);
        items();if(expanded){player.set_text("text_l",digits(quantity_(4)),20,16,cyan);player.text_property("text_l",3,0);}
        panels_[3]->player.looping(true);
        active_=shown_=true;closing_=false;slide_=0;toggle_=-1;scale_=1.05f;
        notify_(34007,6,0);return 1;
    }
    case 34001:
        panels_={};++generation_;closing_=true;slide_=0;return 1;
    case 34003:
        panels_={};++generation_;targets_.reset();active_=closing_=false;shown_=true;slide_=toggle_=-1;scale_=1.05f;return 0;
    case 32770:
        if(toggle_<0){toggle_=0;hide_=shown_;}return 1;
    case 32771:
        // Native 14b740 treats this as the authoritative selection, including
        // negative enemy indices which clear it. The HUD must follow the
        // battle script instead of maintaining a separate logical target.
        targets_.select(first,second);
        return 0;
    case 32773:ammunition(first);return 0;
    case 32775:items();return 0;
    default:throw std::runtime_error("Combat panel protocol not implemented: "+std::to_string(message));
    }
}
bool CombatPanel::advance(){
    if(slide_>=0){
        if(slide_<10){notify_(34005,-320,closing_?-200-slide_*2:-220+slide_*2);++slide_;}
        else {if(closing_){notify_(34005,-320,-220);active_=false;}slide_=-1;}
    }
    if(toggle_>=0){
        if(toggle_==0){scale_=hide_?1.05f:0.f;if(!hide_)shown_=true;}
        if(toggle_<(hide_?10:11)){if(toggle_>0)scale_+=hide_?-.1f:.1f;++toggle_;}
        else{if(hide_)shown_=false;toggle_=-1;}
    }
    for(auto& panel:panels_)if(panel)panel->player.advance();
    // Store the scale for this frame before incrementing the next callback's
    // value; callback 14c3d0/14c4a8 performs repeated float additions.
    return active_;
}
std::vector<CombatPanel::Draw> CombatPanel::compose(const SceneCamera& camera){
    std::vector<Draw> out;if(!visible())return out;
    const auto append=[&](unsigned panel,float x,float y,float sx,float sy){
        for(auto draw:panels_[panel]->player.compose()){
            for(auto& v:draw.vertices){v.x=x+v.x*sx;v.y=y+v.y*sy;}out.push_back({panel,std::move(draw)});
        }
    };
    const auto forward=math::normalize_xyz(camera.target+camera.eye*(-1.f));
    const auto cr=math::normalize_xyz(math::cross_xyz(forward,camera.up));
    const auto up=math::cross_xyz(cr,forward),right=cr*(camera.mirror_horizontal?-1.f:1.f);
    const auto marker=[&](unsigned panel,const CombatTargets::Marker& m,bool health){
        const math::Vector4 delta{m.position[0]-camera.eye.x,m.position[1]-camera.eye.y,m.position[2]-camera.eye.z,0};
        const float depth=math::dot_xyz(delta,forward);if(depth<=camera.near_plane)return;
        const float s=.1f*camera.focal_pixels/depth;
        const float x=320+math::dot_xyz(delta,right)*camera.focal_pixels/depth,y=224-math::dot_xyz(delta,up)*camera.focal_pixels/depth;
        if(health)targets_.apply_health(panels_[panel]->player);
        else panels_[panel]->player.set_text("text_EN",m.enemy<0?"":std::to_string(m.enemy+1),9,16,{.02734375f,.796875f,.796875f,1});
        append(panel,x-32*s,y-32*s,s,s);
        if(health)for(const auto& [depth,layer]:panels_[panel]->player.layers()){
            const auto text=panels_[panel]->movie.texts.find(layer.shape);
            if(text==panels_[panel]->movie.texts.end()||text->second.variable!="text_el")continue;
            const float hp=targets_.draw_health();const math::Vector4 color{std::clamp(2*(1-hp),0.f,1.f),std::clamp(2*hp,0.f,1.f),0,.8f};
            FlashDraw bar;bar.indices={0,1,2,2,1,3};
            for(const auto point:std::array<math::Vector4,4>{{{.5f,3.75f,0,1},{.5f+55*hp,3.75f,0,1},{.5f,6.75f,0,1},{.5f+55*hp,6.75f,0,1}}}){
                const auto p=math::transform(layer.matrix,point);bar.vertices.push_back({x+(p.x-32)*s,y+(p.y-32)*s,0,0,color});}
            out.push_back({panel,std::move(bar)});
        }
    };
    if(targets_.enemy()>=0)marker(1,targets_.selected_marker(),true);
    for(unsigned i=0;i<3;++i)if(int(i)!=targets_.enemy()&&targets_.groups()[i].health>0)marker(4+i,targets_.markers()[i],false);
    append(0,0,4,1,1);
    if(shown_){targets_.apply(panels_[2]->player);append(2,0,374,1,scale_);const auto p=targets_.selection_position();append(3,320+p[0],224+p[1],1,scale_);}
    return out;
}
}
