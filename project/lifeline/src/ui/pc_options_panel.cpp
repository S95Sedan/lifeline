#include "ui/menus.hpp"
#include "ui/settings_typography.hpp"
#include <algorithm>
#include <cctype>

namespace lifeline {
namespace {constexpr auto teal=settings_teal,white=settings_white;}
void OptionsController::refresh_panel(){
    ui_.width=360;ui_.height=448;ui_.shapes.clear();ui_.texts.clear();ui_.frames={FlashFrame{}};
    unsigned id=10000;
    const auto shape=[&](float x,float y,float w,float h,math::Vector4 color){
        FlashFill fill;fill.color=color;fill.polygons={{{x,y,0,1},{x+w,y,0,1},{x+w,y+h,0,1},{x,y+h,0,1}}};
        ui_.shapes.emplace(id,FlashShape{{x,y,x+w,y+h},{fill}});FlashPlacement p;p.depth=id;p.shape=id;p.flags=2;ui_.frames[0].commands.push_back(p);++id;
    };
    const auto outline=[&](float x,float y,float w,float h,math::Vector4 color){shape(x,y,w,1,color);shape(x,y+h,w,1,color);shape(x,y,1,h,color);shape(x+w,y,1,h,color);};
    std::vector<std::pair<std::string,FlashTextValue>> values;
    const auto text=[&](std::string value,float x,float y,float w,float size,math::Vector4 color=teal,bool caption=true,bool fit=true){
        const auto font=caption?caption_font_:body_font_;
        if(caption)value=settings_caption(std::move(value));
        float extent=0,max_extent=0;const auto& f=ui_.fonts.at(font);
        for(unsigned char c:value){if(c=='\n'){max_extent=std::max(max_extent,extent);extent=0;continue;}if(c==' '){extent+=.25f;continue;}const auto found=std::find(f.characters.begin(),f.characters.end(),unsigned(c));if(found!=f.characters.end())extent+=f.advances[found-f.characters.begin()];}
        const float width=fit&&std::max(extent,max_extent)*size>w?w/std::max(extent,max_extent):size;
        const auto name="panel_"+std::to_string(id);FlashText t;t.variable=name;t.font=font;t.bounds={x,y,w,90};t.height=size;
        ui_.texts.emplace(id,std::move(t));FlashPlacement p;p.depth=id;p.shape=id;p.flags=2;ui_.frames[0].commands.push_back(p);
        values.emplace_back(name,FlashTextValue{std::move(value),width,size,color});++id;
    };
    shape(8,8,344,432,{0,.025f,.022f,.94f});outline(8,8,344,432,teal);
    shape(20,20,9,30,teal);
    const auto heading=page_==Page::main?"SETTINGS":page_==Page::controls?"CONTROLS":page_==Page::display?"DISPLAY":page_==Page::graphics?"GRAPHICS":page_==Page::microphone?"MICROPHONE":"AUDIO";
    text(heading,40,19,294,34);shape(20,57,320,1,teal);
    std::vector<std::string> labels,right;std::string help;
    if(page_==Page::controls){
        text("ACTION",33,79,86,14);text("KEYBOARD",128,79,96,14);text("JOYSTICK",234,79,101,14);
        for(unsigned i=0;i<input_action_count;++i){std::string label=input_action_label(InputAction(i));for(auto& c:label)c=char(std::toupper(static_cast<unsigned char>(c)));labels.push_back(std::move(label));}
        labels.insert(labels.end(),{"RESTORE DEFAULTS","BACK"});
        help=capturing_?(column_?"Press a joystick button or move an axis.\nEscape cancels capture.":"Press a keyboard key. Escape cancels.\nDelete clears this binding."):
            row_<input_action_count?"Left / Right: choose keyboard or joystick.\nEnter: rebind. Delete: clear.\nChanges save immediately.":row_==input_action_count?"Restore default keyboard and joystick inputs.":"Return to Settings.";
    }else{
        const auto rows=settings_rows();for(const auto& row:rows){labels.push_back(row.label);right.push_back(row.value);}
        if(!rows.empty())help=rows.at(row_).help;
    }
    const float top=page_==Page::controls?104.f:70.f,step=settings_row_height();
    const auto end=std::min(unsigned(labels.size()),first_row_+visible_rows);
    for(unsigned row=first_row_;row<end;++row){
        const float y=settings_row_y(row);const auto color=row==row_?white:teal;
        outline(19,y+5,8,8,color);if(row==row_)shape(22,y+8,3,3,white);
        if(page_==Page::controls&&row<input_action_count){
            text(labels[row],33,y,85,17,color);
            for(unsigned col=0;col<2;++col){const float x=col?234.f:128.f,w=col?99.f:93.f;
                if(row==row_&&col==column_)outline(x-3,y-2,w+4,25,white);
                text(row==row_&&col==column_&&capturing_?"Waiting...":binding_label((col?settings_->bindings.joystick:settings_->bindings.keyboard)[row]),x,y,w,20,row==row_&&col==column_?white:teal);
            }
        }else if(row<right.size()&&!right[row].empty()){
            text(labels[row],33,y,298,15,color);
            if((page_==Page::sound||page_==Page::microphone)&&row==0){
                text("<",33,y+17,23,23,color,true,false);
                text(right[row],56,y+17,252,23,color);
                text(">",308,y+17,23,23,color,true,false);
            }else text("< "+right[row]+" >",33,y+17,298,23,color);
        }else text(labels[row],33,y,298,22,color);
    }
    if(page_==Page::microphone){
        text("INPUT LEVEL",33,116,110,12);
        const auto color=microphone_clipping_?math::Vector4{1,.25f,.2f,1}:teal;
        text(!microphone_error_.empty()?"UNAVAILABLE":!microphone_listening_?"PAUSED":microphone_clipping_?"TOO LOUD":"SPEAK NORMALLY",145,116,186,12,color);
        outline(33,135,298,10,teal);shape(34,136,296,8,{0,.1f,.09f,1});
        const float fill=296*microphone_level_/100.f;
        shape(34,136,std::min(fill,236.8f),8,{.1f,.85f,.45f,1});
        if(fill>236.8f)shape(270.8f,136,std::min(fill-236.8f,44.4f),8,{1,.8f,.15f,1});
        if(fill>281.2f)shape(315.2f,136,fill-281.2f,8,{1,.25f,.2f,1});
    }
    if(labels.size()>visible_rows){
        const float track=step*visible_rows-7,thumb=track*visible_rows/labels.size();
        shape(344,top+5,2,track,{0,.22f,.2f,1});shape(343,top+5+(track-thumb)*first_row_/(labels.size()-visible_rows),3,thumb,teal);
        text(std::to_string(first_row_+1)+" - "+std::to_string(end)+" / "+std::to_string(labels.size()),239,42,96,12);
    }
    outline(20,328,320,76,teal);text("MESSAGE",28,330,100,12);text(!notice_.empty()?notice_:page_==Page::microphone&&!microphone_error_.empty()?"Microphone unavailable: "+microphone_error_:help,28,350,300,16,white,false);
    text("Arrows / Wheel: select   Enter: set",20,412,320,13,teal,false);
    text("Escape: back / resume",20,428,320,10,teal,false);
    root_=std::make_unique<FlashPlayer>(ui_);for(auto& [name,value]:values)root_->set_text(std::move(name),std::move(value.value),value.width,value.height,value.color);
}
}
