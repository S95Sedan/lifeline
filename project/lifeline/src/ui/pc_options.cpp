#include "ui/menus.hpp"
#include "ui/settings_typography.hpp"
#include <algorithm>
#include <cctype>

namespace lifeline {
namespace {
constexpr auto teal=settings_teal,white=settings_white;
constexpr unsigned scales[]{50,75,100,125,150,200};
constexpr float row_step=32;
std::string upper(std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;}
}
OptionsController::OptionsController(const OptionsAssets& a,MenuSettings& s,AppConfig graphics,SDL_DisplayID display_id,Layout layout)
    :assets_(&a),settings_(&s),layout_(layout),graphics_(graphics),pending_graphics_(graphics),ui_(a.movies.at("op_title.cf")){
    body_font_=ui_.texts.begin()->second.font;
    for(const auto& [key,text]:ui_.texts)if(text.variable=="text_op1")body_font_=text.font;
    caption_font_=add_settings_caption_font(ui_,body_font_);
    ui_.buttons.clear();ui_.timelines.clear();display(display_id?display_id:SDL_GetPrimaryDisplay());open_main();
}
void OptionsController::display(SDL_DisplayID id){
    display_=id;fullscreen_resolutions_.clear();int count=0;
    output_resolution_={0,0};if(const auto* desktop=id?SDL_GetDesktopDisplayMode(id):nullptr)output_resolution_={desktop->w,desktop->h};
    const auto limit=display_resolution_limit(id);
    resolutions_={{640,480},{800,600},{960,672},{960,720},{1024,768},{1280,960},{1440,1080},{1600,1200},{1920,1440},{2560,1920},{2880,2160},{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}};
    const std::pair current{pending_graphics_.width,pending_graphics_.height};resolutions_.push_back(current);
    if(output_resolution_.first)resolutions_.push_back(output_resolution_);
    const auto allowed=[&](const auto& size){return (!output_resolution_.first||(size.first<=output_resolution_.first&&size.second<=output_resolution_.second))&&(!limit.first||(size.first<=limit.first&&size.second<=limit.second));};
    std::erase_if(resolutions_,[&](const auto& size){return !allowed(size);});
    std::sort(resolutions_.begin(),resolutions_.end());resolutions_.erase(std::unique(resolutions_.begin(),resolutions_.end()),resolutions_.end());
    borderless_resolutions_=borderless_render_resolutions(limit.first?std::min(output_resolution_.first,limit.first):output_resolution_.first,limit.second?std::min(output_resolution_.second,limit.second):output_resolution_.second,pending_graphics_.aspect_ratio);
    std::erase_if(borderless_resolutions_,[&](const auto& size){return !allowed(size);});
    if(auto** modes=id?SDL_GetFullscreenDisplayModes(id,&count):nullptr){
        for(int i=0;i<count;++i){const auto& mode=*modes[i];if(mode.pixel_density==1.f&&mode.w>=320&&mode.h>=320&&mode.w<=7680&&mode.h<=7680&&(!limit.first||(mode.w<=limit.first&&mode.h<=limit.second)))fullscreen_resolutions_.emplace_back(mode.w,mode.h);}
        SDL_free(modes);
    }
    std::sort(fullscreen_resolutions_.begin(),fullscreen_resolutions_.end());
    fullscreen_resolutions_.erase(std::unique(fullscreen_resolutions_.begin(),fullscreen_resolutions_.end()),fullscreen_resolutions_.end());
    if(pending_graphics_.mode==WindowMode::fullscreen)select_fullscreen_resolution();else select_window_resolution();
    if(root_)refresh();
}
void OptionsController::select_window_resolution(){
    const auto& sizes=pending_graphics_.mode==WindowMode::borderless?borderless_resolutions_:resolutions_;
    if(sizes.empty())return;
    const std::pair current{pending_graphics_.width,pending_graphics_.height};
    if(std::find(sizes.begin(),sizes.end(),current)==sizes.end())std::tie(pending_graphics_.width,pending_graphics_.height)=sizes.back();
}
bool OptionsController::select_fullscreen_resolution(){
    if(fullscreen_resolutions_.empty()){const auto audio=pending_graphics_.audio;const auto devices=pending_graphics_.devices;pending_graphics_=graphics_;pending_graphics_.audio=audio;pending_graphics_.devices=devices;notice_="No fullscreen resolutions available for this display.";return false;}
    const std::pair current{pending_graphics_.width,pending_graphics_.height};
    if(std::find(fullscreen_resolutions_.begin(),fullscreen_resolutions_.end(),current)!=fullscreen_resolutions_.end())return true;
    auto resolution=fullscreen_resolutions_.back();
    if(const auto* desktop=SDL_GetDesktopDisplayMode(display_)){
        const std::pair size{desktop->w,desktop->h};if(std::find(fullscreen_resolutions_.begin(),fullscreen_resolutions_.end(),size)!=fullscreen_resolutions_.end())resolution=size;
    }
    std::tie(pending_graphics_.width,pending_graphics_.height)=resolution;return true;
}
unsigned OptionsController::row_count()const{
    switch(page_){
    case Page::main:return 6;
    case Page::controls:return input_action_count+2;
    case Page::display:return 6;
    case Page::graphics:return 10;
    case Page::sound:return 7;
    case Page::microphone:return 4;
    default:return 0;
    }
}
void OptionsController::reveal_selection(){
    if(row_<first_row_)first_row_=row_;
    if(row_>=first_row_+visible_rows)first_row_=row_-visible_rows+1;
    const auto count=row_count();first_row_=std::min(first_row_,count>visible_rows?count-visible_rows:0);
}
void OptionsController::open_main(){open(Page::main);}
float OptionsController::settings_row_height()const{
    if(layout_==Layout::full_screen)return row_step;
    return page_==Page::microphone?52.f:(page_==Page::graphics||page_==Page::display)?42.f:page_==Page::controls?32.f:38.f;
}
float OptionsController::settings_row_y(unsigned row)const{
    const bool panel=layout_==Layout::side_panel;
    const float top=panel?(page_==Page::controls?104.f:70.f):(page_==Page::controls?96.f:88.f);
    return top+settings_row_height()*(row-first_row_)+(page_==Page::microphone&&row?panel?44.f:40.f:0.f);
}
void OptionsController::microphone_meter(unsigned level,bool clipping,bool listening,const std::string& error){
    level=std::min(level,100u);
    if(level==microphone_level_&&clipping==microphone_clipping_&&listening==microphone_listening_&&error==microphone_error_)return;
    microphone_level_=level;microphone_clipping_=clipping;microphone_listening_=listening;microphone_error_=error;
    if(page_==Page::microphone)refresh();
}
void OptionsController::refresh_audio_devices(){
    try{playback_devices_=audio_device_choices(false);microphone_devices_=audio_device_choices(true);}
    catch(const std::exception& error){playback_devices_={{{},"System default (unavailable)",0}};microphone_devices_=playback_devices_;notice_=error.what();}
}
void OptionsController::open(Page page){
    if(page_==Page::main)main_row_=row_;
    page_=page;row_=page==Page::main?main_row_:0;first_row_=column_=0;capturing_=false;notice_.clear();dialog_.reset();audio_preview_.reset();if(page==Page::sound||page==Page::microphone)refresh_audio_devices();refresh();
}
void OptionsController::refresh(){
    if(layout_==Layout::side_panel){refresh_panel();return;}
    ui_.shapes.clear();ui_.texts.clear();ui_.frames={FlashFrame{}};unsigned id=10000;
    const auto shape=[&](float x,float y,float w,float h,math::Vector4 color){
        FlashFill fill;fill.color=color;fill.polygons={{{x,y,0,1},{x+w,y,0,1},{x+w,y+h,0,1},{x,y+h,0,1}}};
        ui_.shapes.emplace(id,FlashShape{{x,y,x+w,y+h},{fill}});FlashPlacement p;p.depth=id;p.shape=id;p.flags=2;ui_.frames[0].commands.push_back(p);++id;
    };
    const auto outline=[&](float x,float y,float w,float h,math::Vector4 c){shape(x,y,w,1,c);shape(x,y+h,w,1,c);shape(x,y,1,h,c);shape(x+w,y,1,h,c);};
    std::vector<std::pair<std::string,FlashTextValue>> values;
    const auto text=[&](std::string value,float x,float y,float w,float width,float height,math::Vector4 color=teal,bool caption=true,bool fit=true){
        const auto font=caption?caption_font_:body_font_;
        if(caption)value=settings_caption(std::move(value));
        width=std::min(width,height);
        // Fit long device names in their column without wrapping into the next row.
        float extent=0;const auto& f=ui_.fonts.at(font);
        for(unsigned char c:value){if(c=='\n'){extent=0;break;}if(c==' '){extent+=.25f;continue;}const auto found=std::find(f.characters.begin(),f.characters.end(),unsigned(c));if(found!=f.characters.end())extent+=f.advances[found-f.characters.begin()];}
        if(fit&&extent*width>w)width=w/extent;
        const auto name="pc_"+std::to_string(id);FlashText t;t.variable=name;t.font=font;t.bounds={x,y,w,100};t.height=height;
        ui_.texts.emplace(id,std::move(t));FlashPlacement p;p.depth=id;p.shape=id;p.flags=2;ui_.frames[0].commands.push_back(p);
        values.emplace_back(name,FlashTextValue{std::move(value),width,height,color});++id;
    };
    // Retain the original row spacing, connected markers and tall message panel.
    outline(16,36,607,316,teal);shape(32,16,14,40,{0,0,0,1});outline(32,16,14,40,teal);
    shape(58,10,262,46,{0,0,0,1});
    const auto heading=page_==Page::main?"OPTION":page_==Page::controls?"CONTROLS":page_==Page::display?"DISPLAY":page_==Page::graphics?"GRAPHICS":page_==Page::microphone?"MICROPHONE":"AUDIO";
    text(heading,66,12,252,58,42);
    std::vector<std::string> labels,help,right;std::string description;
    const float y=page_==Page::controls?96.f:88.f;
    if(page_!=Page::controls){
        const auto rows=settings_rows();for(const auto& row:rows){labels.push_back(row.label);right.push_back(row.value);}
        if(!rows.empty())description=rows.at(row_).help;
    }
    if(page_==Page::controls){
        text("ACTION",70,68,204,25,17);text("KEYBOARD",290,68,135,25,17);text("JOYSTICK",440,68,155,25,17);
        for(unsigned i=0;i<input_action_count;++i)labels.push_back(upper(input_action_label(InputAction(i))));
        labels.insert(labels.end(),{"RESTORE DEFAULTS","BACK"});
        description=capturing_?(column_?"Press a joystick button or move an axis.\nEscape: cancel capture.":"Press a keyboard key.\nEscape: cancel capture. Delete: clear binding."):
            row_<input_action_count?"Left/Right or Tab: choose input. Enter: rebind.\nDelete: clear. Conflicting inputs swap.\nBindings take effect and save immediately.":
            row_==input_action_count?"Restore all keyboard and joystick bindings.\nThe defaults take effect immediately.":"Return to Options. Your bindings are already saved.";
    }
    const auto end=std::min(unsigned(labels.size()),first_row_+visible_rows);
    if(end>first_row_)shape(39,56,1,settings_row_y(end-1)+12-56,teal);
    for(unsigned i=first_row_;i<end;++i){
        const float py=settings_row_y(i);const bool selected=i==row_;
        shape(39,py+12,10,1,teal);outline(49,py+3,14,14,selected?white:teal);if(selected)shape(54,py+8,5,5,white);
        text(labels[i],70,py,page_==Page::controls?204.f:(page_==Page::graphics||page_==Page::display||page_==Page::sound||page_==Page::microphone)?245.f:510.f,32,24,selected?white:teal);
        if(page_==Page::controls&&i<input_action_count){
            for(unsigned col=0;col<2;++col){
                const float px=col?440.f:290.f,w=col?153.f:135.f;
                if(selected&&column_==col)outline(px-5,py-2,w+5,27,white);
                text(selected&&column_==col&&capturing_?"Waiting...":binding_label((col?settings_->bindings.joystick:settings_->bindings.keyboard)[i]),px,py,w,29,23,selected&&column_==col?white:teal);
            }
        }else if(i<right.size()&&!right[i].empty()){
            const auto color=selected?white:teal;
            if((page_==Page::sound||page_==Page::microphone)&&i==0){
                // Device names may shrink to fit; navigation glyphs keep the
                // same width as the other Audio/Microphone value arrows.
                text("<",330,py,24,32,24,color,true,false);
                text(right[i],354,py,216,32,24,color);
                text(">",570,py,24,32,24,color,true,false);
            }else text("< "+right[i]+" >",330,py,264,32,24,color);
        }
    }
    if(page_==Page::microphone){
        text("INPUT LEVEL",70,120,240,18,14);
        const auto color=microphone_clipping_?math::Vector4{1,.25f,.2f,1}:teal;
        text(!microphone_error_.empty()?"UNAVAILABLE":!microphone_listening_?"PAUSED":microphone_clipping_?"TOO LOUD":"SPEAK NORMALLY",330,120,264,18,14,color);
        outline(70,140,524,10,teal);shape(71,141,522,8,{0,.1f,.09f,1});
        const float fill=522*microphone_level_/100.f;
        shape(71,141,std::min(fill,417.6f),8,{.1f,.85f,.45f,1});
        if(fill>417.6f)shape(488.6f,141,std::min(fill-417.6f,78.3f),8,{1,.8f,.15f,1});
        if(fill>495.9f)shape(566.9f,141,fill-495.9f,8,{1,.25f,.2f,1});
    }
    if(labels.size()>visible_rows){
        text(std::to_string(first_row_+1)+" - "+std::to_string(end)+" / "+std::to_string(labels.size()),500,47,99,20,14);
        constexpr float track_height=164;const float thumb=track_height*visible_rows/labels.size();
        shape(606,y+12,2,track_height,{0,.22f,.2f,1});
        shape(605,y+12+(track_height-thumb)*first_row_/(labels.size()-visible_rows),4,thumb,teal);
        if(first_row_)text("^",602,y-9,15,22,16);
        if(end<labels.size())text("v",602,y+176,15,22,16);
    }
    shape(33,290,574,124,{0,.055f,.05f,1});outline(33,290,574,124,teal);
    text("MESSAGE",56,290,118,24,14);shape(33,304,574,1,teal);shape(154,290,1,14,teal);outline(597,295,5,5,teal);
    text(!notice_.empty()?notice_:page_==Page::microphone&&!microphone_error_.empty()?"Microphone unavailable: "+microphone_error_:description,48,322,545,26,19,white,false);
    text("Arrows / Wheel: Select   Enter: Set   Escape: Back",34,423,574,25,18,teal,false);
    root_=std::make_unique<FlashPlayer>(ui_);
    for(auto& [name,value]:values)root_->set_text(std::move(name),std::move(value.value),value.width,value.height,value.color);
}
void OptionsController::advance(){}
void OptionsController::graphics_applied(){graphics_=pending_graphics_;}
void OptionsController::move(int horizontal,int vertical){
    if(!ready()||capturing_)return;notice_.clear();
    if(vertical){row_=unsigned(std::clamp(int(row_)+vertical,0,int(row_count())-1));reveal_selection();}
    if(horizontal){
        const int direction=horizontal>0?1:-1;
        if(page_==Page::controls)column_=horizontal>0?1:0;
        const auto cycle=[&](std::string& value,const std::vector<AudioDeviceChoice>& choices){
            if(choices.empty())return;
            const auto at=std::find_if(choices.begin(),choices.end(),[&](const auto& choice){return choice.key==value;});
            const int index=at==choices.end()?0:int(at-choices.begin());value=choices[(index+direction+int(choices.size()))%int(choices.size())].key;
        };
        if(page_==Page::sound&&row_==0)cycle(pending_graphics_.devices.playback,playback_devices_);
        if(page_==Page::sound&&row_==1)settings_->sound=1-settings_->sound;
        if(page_==Page::microphone&&row_==0)cycle(pending_graphics_.devices.microphone,microphone_devices_);
        if(page_==Page::microphone&&row_==1)pending_graphics_.devices.microphone_gain=unsigned(std::clamp(int(pending_graphics_.devices.microphone_gain)+direction*5,0,200));
        if(page_==Page::sound&&row_>=2&&row_<=4){
            auto& audio=pending_graphics_.audio;
            auto& volume=row_==2?audio.fmv:row_==3?audio.dialogue:audio.music;
            volume=unsigned(std::clamp(int(volume)+direction*5,0,100));
            audio_preview_=row_==2?AudioBus::fmv:row_==3?AudioBus::dialogue:AudioBus::music;
        }
        if(page_==Page::display){
            if(row_==0){pending_graphics_.mode=WindowMode((int(pending_graphics_.mode)+direction+3)%3);if(pending_graphics_.mode==WindowMode::fullscreen)select_fullscreen_resolution();else select_window_resolution();}
            if(row_==1){
                const auto& sizes=pending_graphics_.mode==WindowMode::fullscreen?fullscreen_resolutions_:pending_graphics_.mode==WindowMode::borderless?borderless_resolutions_:resolutions_;
                if(!sizes.empty()){
                    auto at=std::find(sizes.begin(),sizes.end(),std::pair{pending_graphics_.width,pending_graphics_.height});
                    int index=at==sizes.end()?0:int(at-sizes.begin());index=(index+direction+int(sizes.size()))%int(sizes.size());
                    std::tie(pending_graphics_.width,pending_graphics_.height)=sizes[index];
                }
            }
            if(row_==2)pending_graphics_.vsync=!pending_graphics_.vsync;
            if(row_==3){pending_graphics_.aspect_ratio=AspectRatio((int(pending_graphics_.aspect_ratio)+direction+3)%3);display(display_);}
            refresh();return;
        }
        if(page_==Page::graphics){
            if(row_==0){const auto current=graphics_preset(pending_graphics_);apply_graphics_preset(pending_graphics_,current==4?(direction>0?2:1):unsigned((int(current)+direction+4)%4));}
            if(row_==1){auto at=std::find(std::begin(scales),std::end(scales),pending_graphics_.render_scale);int index=at==std::end(scales)?2:int(at-std::begin(scales));pending_graphics_.render_scale=scales[(index+direction+6)%6];}
            if(row_==2)pending_graphics_.quality.anti_aliasing=1-pending_graphics_.quality.anti_aliasing;
            if(row_==3)pending_graphics_.quality.texture_quality=unsigned((int(pending_graphics_.quality.texture_quality)+direction+3)%3);
            if(row_==4)pending_graphics_.texture_filter=unsigned((int(pending_graphics_.texture_filter)+direction+3)%3);
            if(row_==5)pending_graphics_.quality.shadow_quality=unsigned((int(pending_graphics_.quality.shadow_quality)+direction+5)%5);
            if(row_==6){const unsigned levels[]{0,2,1};const auto current=pending_graphics_.quality.reflections==0?0:pending_graphics_.quality.reflections==2?1:2;pending_graphics_.quality.reflections=levels[(current+direction+3)%3];}
            if(row_==7)pending_graphics_.quality.visual_style=unsigned((int(pending_graphics_.quality.visual_style)+direction+5)%5);
            refresh();return;
        }
    }
    refresh();
}
void OptionsController::confirm(){
    if(!ready()||capturing_)return;notice_.clear();
    if(page_==Page::main){
        switch(row_){case 0:open(Page::sound);break;case 1:open(Page::controls);break;case 2:open(Page::display);break;case 3:open(Page::graphics);break;case 4:open(Page::microphone);break;
        default:page_=Page::closed;}return;
    }
    if(page_==Page::controls){
        if(row_<input_action_count){capturing_=true;refresh();}
        else if(row_==input_action_count){settings_->bindings={};input_bindings()=settings_->bindings;notice_="Default bindings restored and saved.";refresh();}
        else back();return;
    }
    if(page_==Page::display){
        if(row_<4){move(1,0);return;}
        if(row_==4){const AppConfig defaults;pending_graphics_.mode=defaults.mode;pending_graphics_.width=defaults.width;pending_graphics_.height=defaults.height;pending_graphics_.vsync=defaults.vsync;pending_graphics_.aspect_ratio=defaults.aspect_ratio;display(display_);refresh();}
        else back();return;
    }
    if(page_==Page::graphics){
        if(row_<8){move(1,0);return;}
        if(row_==8){apply_graphics_preset(pending_graphics_,2);pending_graphics_.quality.visual_style=0;refresh();}
        else back();return;
    }
    if(page_==Page::sound){
        if(row_<5)move(1,0);
        else if(row_==5){settings_->sound=MenuSettings{}.sound;pending_graphics_.audio={};pending_graphics_.devices.playback.clear();notice_="Default playback settings restored and saved.";refresh();}
        else back();return;
    }
    if(page_==Page::microphone){
        if(row_<2)move(1,0);
        else if(row_==2){pending_graphics_.devices.microphone.clear();pending_graphics_.devices.microphone_gain=100;notice_="Default microphone settings restored and saved.";refresh();}
        else back();return;
    }
}
void OptionsController::back(){
    if(capturing_){capturing_=false;refresh();return;}
    if(page_==Page::main){page_=Page::closed;audio_preview_.reset();}
    else if(page_!=Page::closed)open_main();
}
void OptionsController::audio_preview_failed(const std::string& message){
    const auto text="Preview unavailable: "+message;
    if(notice_!=text){notice_=text;refresh();}
}
void OptionsController::display_failed(std::string message){
    const auto audio=pending_graphics_.audio;
    const auto devices=pending_graphics_.devices;
    pending_graphics_=graphics_;
    pending_graphics_.audio=audio;
    pending_graphics_.devices=devices;
    notice_=std::move(message);refresh();
}
void OptionsController::audio_devices_failed(const AudioDeviceSettings& previous,const std::string& message){pending_graphics_.devices=previous;notice_="Audio device unavailable: "+message;refresh();}
bool OptionsController::event(const SDL_Event& e,SDL_JoystickID selected){
    if(!ready())return false;
    if((e.type==SDL_EVENT_AUDIO_DEVICE_ADDED||e.type==SDL_EVENT_AUDIO_DEVICE_REMOVED||e.type==SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED)&&(page_==Page::sound||page_==Page::microphone)){refresh_audio_devices();refresh();return false;}
    if(e.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED){display(SDL_DisplayID(e.window.data1));return false;}
    if(e.type==SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED&&e.display.displayID==display_){display(display_);return false;}
    if(!capturing_){
        if(e.type==SDL_EVENT_MOUSE_WHEEL){
            const float delta=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e.wheel.y:e.wheel.y;
            if(std::isfinite(delta)&&delta!=0)move(0,delta>0?-std::max(1,int(std::min(delta,100.f))):std::max(1,int(std::min(-delta,100.f))));
            return true;
        }
        if(e.type==SDL_EVENT_KEY_DOWN){
            if(e.key.key==SDLK_PAGEUP||e.key.key==SDLK_PAGEDOWN){move(0,e.key.key==SDLK_PAGEUP?-int(visible_rows):int(visible_rows));return true;}
            if(e.key.key==SDLK_HOME||e.key.key==SDLK_END){move(0,e.key.key==SDLK_HOME?-int(row_count()):int(row_count()));return true;}
        }
    }
    if(page_!=Page::controls)return false;
    if(e.type==SDL_EVENT_GAMEPAD_ADDED||e.type==SDL_EVENT_JOYSTICK_ADDED)return false;
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST||e.type==SDL_EVENT_GAMEPAD_REMOVED||e.type==SDL_EVENT_JOYSTICK_REMOVED){if(capturing_){capturing_=false;notice_="Binding cancelled.";refresh();}return false;}
    if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat){
        if(e.key.key==SDLK_ESCAPE&&capturing_){back();return true;}
        if(e.key.key==SDLK_DELETE&&row_<input_action_count){settings_->bindings.assign(InputAction(row_),column_,{});input_bindings()=settings_->bindings;capturing_=false;notice_="Binding cleared and saved.";refresh();return true;}
        if(e.key.key==SDLK_TAB&&!capturing_){column_=1-column_;refresh();return true;}
    }
    if(capturing_){
        if(auto binding=capture_binding(e,column_,selected)){settings_->bindings.assign(InputAction(row_),column_,*binding);input_bindings()=settings_->bindings;capturing_=false;notice_="Binding set and saved.";refresh();}return true;
    }
    return false;
}
void OptionsController::pointer(float x,float y,bool click){
    const bool panel=layout_==Layout::side_panel;
    if(!ready()||capturing_||x<(panel?16.f:33.f)||x>(panel?344.f:598.f))return;
    unsigned row=first_row_;const auto end=std::min(row_count(),first_row_+visible_rows);
    for(;row<end;++row){const auto top=settings_row_y(row);const auto height=page_==Page::microphone&&row==0?(panel?42.f:32.f):settings_row_height();if(y>=top&&y<top+height)break;}
    if(row==end)return;
    const auto col=page_==Page::controls&&row<input_action_count?(x>=(panel?230.f:435.f)?1u:0u):column_;
    if(row_==row&&column_==col&&!click)return;
    row_=row;column_=col;refresh();if(click)confirm();
}
}
