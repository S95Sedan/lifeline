#include "app/config.hpp"
#include <SDL3/SDL.h>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace lifeline {
namespace {
std::string trim(std::string value) {
    const auto begin=value.find_first_not_of(" \t\r\n");
    if(begin==std::string::npos)return {};
    return value.substr(begin,value.find_last_not_of(" \t\r\n")-begin+1);
}
int dimension(const std::string& value) {
    int result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<320 || result>7680)
        throw std::runtime_error("Resolution dimensions must be integers between 320 and 7680");
    return result;
}
void checked(bool success) {
    if(!success)throw std::runtime_error(std::string("Window configuration: ")+SDL_GetError());
}
}
AppConfig& app_config(){static AppConfig value;return value;}
std::filesystem::path user_config_path(){if(const auto* directory=SDL_getenv("LIFELINE_USER_DATA")){const std::string text=directory;return std::filesystem::path(std::u8string(text.begin(),text.end()))/"config.ini";}const std::string base=SDL_GetBasePath();return std::filesystem::path(std::u8string(base.begin(),base.end()))/"user/config.ini";}
AppConfig parse_app_config(std::istream& input) {
    AppConfig result;
    std::set<std::string> keys;
    std::string line;
    while(std::getline(input,line)) {
        line=trim(line.substr(0,line.find_first_of("#;")));
        if(line.empty())continue;
        const auto equal=line.find('=');
        if(equal==std::string::npos)throw std::runtime_error("Expected key=value in config.ini");
        const auto key=trim(line.substr(0,equal)),value=trim(line.substr(equal+1));
        if(!keys.insert(key).second)throw std::runtime_error("Duplicate config setting: "+key);
        if(key=="debug") {
            if(value!="true" && value!="false")throw std::runtime_error("debug must be true or false");
            result.debug=value=="true";
        } else if(key=="renderer") {
            if(value!="d3d12" && value!="software")throw std::runtime_error("renderer must be d3d12 or software");
            result.renderer=value;
        } else if(key=="width")result.width=dimension(value);
        else if(key=="height")result.height=dimension(value);
        else if(key=="window_mode") {
            if(value=="windowed")result.mode=WindowMode::windowed;
            else if(value=="borderless")result.mode=WindowMode::borderless;
            else if(value=="fullscreen")result.mode=WindowMode::fullscreen;
            else throw std::runtime_error("window_mode must be windowed, borderless or fullscreen");
        } else if(key=="aspect_ratio"){
            if(value=="auto")result.aspect_ratio=AspectRatio::automatic;
            else if(value=="4:3")result.aspect_ratio=AspectRatio::standard;
            else if(value=="16:9")result.aspect_ratio=AspectRatio::widescreen;
            else throw std::runtime_error("aspect_ratio must be auto, 4:3 or 16:9");
        } else if(key=="vsync"){
            if(value!="true"&&value!="false")throw std::runtime_error("vsync must be true or false");result.vsync=value=="true";
        } else if(key=="render_scale"){
            unsigned scale=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),scale);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||(scale!=50&&scale!=75&&scale!=100&&scale!=125&&scale!=150&&scale!=200))throw std::runtime_error("Invalid render scale");result.render_scale=scale;
        } else if(key=="texture_filter"){
            if(value=="authored")result.texture_filter=0;else if(value=="nearest")result.texture_filter=1;else if(value=="linear")result.texture_filter=2;else throw std::runtime_error("Invalid texture filtering");
        } else if(key=="texture_quality"){
            if(value=="low")result.quality.texture_quality=0;else if(value=="medium")result.quality.texture_quality=1;else if(value=="high")result.quality.texture_quality=2;else throw std::runtime_error("Invalid texture quality");
        } else if(key=="shadow_quality"){
            if(value=="off")result.quality.shadow_quality=0;else if(value=="low")result.quality.shadow_quality=1;else if(value=="medium")result.quality.shadow_quality=2;else if(value=="high")result.quality.shadow_quality=3;else if(value=="ultra")result.quality.shadow_quality=4;else throw std::runtime_error("Invalid shadow quality");
        } else if(key=="anti_aliasing"){
            if(value=="off")result.quality.anti_aliasing=0;else if(value=="fxaa")result.quality.anti_aliasing=1;else throw std::runtime_error("Invalid anti-aliasing");
        } else if(key=="visual_style"){
            if(value=="original")result.quality.visual_style=0;else if(value=="faithful")result.quality.visual_style=1;else if(value=="balanced")result.quality.visual_style=2;else if(value=="modern")result.quality.visual_style=3;else if(value=="enhanced")result.quality.visual_style=4;else throw std::runtime_error("Invalid visual style");
        } else if(key=="reflections"){
            if(value=="true"||value=="high")result.quality.reflections=1;else if(value=="false"||value=="off")result.quality.reflections=0;else if(value=="low")result.quality.reflections=2;else throw std::runtime_error("reflections must be off, low or high");
        } else if(key=="playback_device"||key=="microphone_device"){
            const auto selection=value=="default"?std::string{}:value;
            if(!valid_audio_device_key(selection))throw std::runtime_error("Invalid audio device selection");
            (key=="playback_device"?result.devices.playback:result.devices.microphone)=selection;
        } else if(key=="microphone_gain"){
            unsigned gain=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),gain);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||gain>200)throw std::runtime_error("microphone_gain must be an integer between 0 and 200");
            result.devices.microphone_gain=gain;
        } else if(key=="fmv_volume"||key=="dialogue_volume"||key=="music_volume"){
            unsigned volume=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),volume);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||volume>100)throw std::runtime_error(key+" must be an integer between 0 and 100");
            if(key=="fmv_volume")result.audio.fmv=volume;
            else if(key=="dialogue_volume")result.audio.dialogue=volume;
            else result.audio.music=volume;
        } else throw std::runtime_error("Unknown config setting: "+key);
    }
    if(input.bad())throw std::runtime_error("Could not read config.ini");
    return result;
}
void write_app_config(const std::filesystem::path& path,const AppConfig& config){
    std::ostringstream text;
    text<<"# General\n"
        <<"debug="<<(config.debug?"true":"false")<<"\nrenderer="<<config.renderer
        <<"\n\n# Display\nwindow_mode="<<(config.mode==WindowMode::windowed?"windowed":config.mode==WindowMode::borderless?"borderless":"fullscreen")
        <<"\naspect_ratio="<<aspect_ratio_name(config.aspect_ratio)
        <<"\nwidth="<<config.width<<"\nheight="<<config.height<<"\nvsync="<<(config.vsync?"true":"false")
        <<"\n\n# Graphics\nrender_scale="<<config.render_scale<<"\ntexture_filter="<<(config.texture_filter==0?"authored":config.texture_filter==1?"nearest":"linear")
        <<"\ntexture_quality="<<(config.quality.texture_quality==0?"low":config.quality.texture_quality==1?"medium":config.quality.texture_quality==2?"high":"invalid")
        <<"\nshadow_quality="<<(config.quality.shadow_quality==0?"off":config.quality.shadow_quality==1?"low":config.quality.shadow_quality==2?"medium":config.quality.shadow_quality==3?"high":config.quality.shadow_quality==4?"ultra":"invalid")
        <<"\nanti_aliasing="<<(config.quality.anti_aliasing==0?"off":config.quality.anti_aliasing==1?"fxaa":"invalid")<<"\nreflections="<<(config.quality.reflections==0?"false":config.quality.reflections==1?"true":config.quality.reflections==2?"low":"invalid")
        <<"\nvisual_style="<<(config.quality.visual_style==0?"original":config.quality.visual_style==1?"faithful":config.quality.visual_style==2?"balanced":config.quality.visual_style==3?"modern":config.quality.visual_style==4?"enhanced":"invalid")
        <<"\n\n# Audio\nfmv_volume="<<config.audio.fmv<<"\ndialogue_volume="<<config.audio.dialogue<<"\nmusic_volume="<<config.audio.music
        <<"\nplayback_device="<<(config.devices.playback.empty()?"default":config.devices.playback)
        <<"\nmicrophone_device="<<(config.devices.microphone.empty()?"default":config.devices.microphone)
        <<"\nmicrophone_gain="<<config.devices.microphone_gain<<'\n';
    std::istringstream validation(text.str());if(parse_app_config(validation)!=config)throw std::runtime_error("Invalid application settings");
    std::filesystem::create_directories(path.parent_path());const auto temporary=path.string()+".tmp";
    std::ofstream output(temporary,std::ios::trunc);output<<text.str();output.close();if(!output)throw std::runtime_error("Cannot save application settings");
    // A single replacement avoids losing the previous config on write failure.
#ifdef _WIN32
    if(!MoveFileExW(std::filesystem::path(temporary).c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace application settings");
#else
    std::filesystem::rename(temporary,path);
#endif
}
void apply_graphics_preset(AppConfig& config,unsigned preset){
    const auto style=config.quality.visual_style;
    switch(preset){
    case 0:config.render_scale=75;config.texture_filter=1;config.quality={0,0,0,false};break;
    case 1:config.render_scale=100;config.texture_filter=2;config.quality={1,1,0,2};break;
    case 2:config.render_scale=100;config.texture_filter=0;config.quality={};break;
    case 3:config.render_scale=150;config.texture_filter=0;config.quality={2,3,1,true};break;
    default:throw std::runtime_error("Invalid graphics preset");
    }
    config.quality.visual_style=style;
}
unsigned graphics_preset(const AppConfig& config){
    for(unsigned preset=0;preset<4;++preset){auto candidate=config;apply_graphics_preset(candidate,preset);if(candidate==config)return preset;}return 4;
}
void apply_display_settings(SDL_Window* window,const AppConfig& config,bool hidden){
    if(hidden){checked(SDL_SetWindowSize(window,config.width,config.height));return;}
    // Borderless resolution changes affect the scene framebuffer. Keep the
    // desktop-sized window in place instead of leaving and re-entering fullscreen.
    if(config.mode==WindowMode::borderless&&(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)&&!SDL_GetWindowFullscreenMode(window))return;
    SDL_DisplayMode fullscreen{};
    if(config.mode==WindowMode::fullscreen){
        checked(SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),config.width,config.height,0,false,&fullscreen));
        if(fullscreen.w!=config.width||fullscreen.h!=config.height)throw std::runtime_error("This display does not support the selected fullscreen resolution.");
    }
    checked(SDL_SetWindowFullscreen(window,false));checked(SDL_SyncWindow(window));
    checked(SDL_SetWindowFullscreenMode(window,config.mode==WindowMode::fullscreen?&fullscreen:nullptr));
    if(config.mode==WindowMode::windowed)checked(SDL_SetWindowSize(window,config.width,config.height));
    else checked(SDL_SetWindowFullscreen(window,true));
    checked(SDL_SyncWindow(window));
}
std::pair<int,int> display_resolution_limit(SDL_DisplayID display){
    const auto* desktop=display?SDL_GetDesktopDisplayMode(display):nullptr;
    std::pair fallback=desktop?std::pair{desktop->w,desktop->h}:std::pair{0,0};
#ifdef _WIN32
    // Driver mode lists can include supersampling modes larger than the panel.
    // Query the active monitor's preferred target mode rather than treating those
    // synthetic resolutions as physical monitor support.
    const auto monitor=display?static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(display),SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr)):nullptr;
    MONITORINFOEXW info{};info.cbSize=sizeof(info);
    if(monitor&&GetMonitorInfoW(monitor,&info))for(unsigned attempt=0;attempt<2;++attempt){
        UINT32 paths=0,modes=0;if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)!=ERROR_SUCCESS)break;
        std::vector<DISPLAYCONFIG_PATH_INFO> path_info(paths);std::vector<DISPLAYCONFIG_MODE_INFO> mode_info(modes);
        const auto status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,path_info.data(),&modes,mode_info.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;if(status!=ERROR_SUCCESS)break;
        for(unsigned i=0;i<paths;++i){
            const auto& path=path_info[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||std::wstring_view(source.viewGdiDeviceName)!=info.szDevice)continue;
            DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
            preferred.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,sizeof(preferred),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&preferred.header)==ERROR_SUCCESS&&preferred.width>=320&&preferred.height>=320&&preferred.width<=7680&&preferred.height<=7680){
                std::pair size{int(preferred.width),int(preferred.height)};
                if(path.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE90||path.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE270)std::swap(size.first,size.second);
                return size;
            }
        }
        break;
    }
#endif
    return fallback;
}
const char* aspect_ratio_name(AspectRatio ratio){
    switch(ratio){case AspectRatio::automatic:return "auto";case AspectRatio::standard:return "4:3";case AspectRatio::widescreen:return "16:9";default:return "invalid";}
}
std::pair<unsigned,unsigned> presentation_resolution(const AppConfig& config,unsigned width,unsigned height){
    if(config.aspect_ratio==AspectRatio::automatic||!width||!height)return {width,height};
    const unsigned numerator=config.aspect_ratio==AspectRatio::standard?4:16,denominator=config.aspect_ratio==AspectRatio::standard?3:9;
    if(std::uint64_t(width)*denominator>std::uint64_t(height)*numerator)width=unsigned(std::uint64_t(height)*numerator/denominator);
    else height=unsigned(std::uint64_t(width)*denominator/numerator);
    return {std::max(1u,width),std::max(1u,height)};
}
std::vector<std::pair<int,int>> borderless_render_resolutions(int width,int height,AspectRatio ratio){
    std::vector<std::pair<int,int>> sizes;if(width<320||height<320)return sizes;
    AppConfig config;config.aspect_ratio=ratio;const auto viewport=presentation_resolution(config,unsigned(width),unsigned(height));width=int(viewport.first);height=int(viewport.second);
    const auto add=[&](int w,int h){if(w>=320&&h>=320&&w<=width&&h<=height&&w<=7680&&h<=7680)sizes.emplace_back(w,h);};
    for(unsigned percent:{25,50,75,100})add(int(std::lround(width*percent/100.0)),int(std::lround(height*percent/100.0)));
    for(const auto size:{std::pair{640,480},std::pair{800,600},std::pair{960,720},std::pair{1024,768},std::pair{1280,960},std::pair{1440,1080},std::pair{1600,1200},std::pair{1920,1440},std::pair{2560,1920},std::pair{2880,2160},std::pair{960,540},std::pair{1280,720},std::pair{1600,900},std::pair{1920,1080},std::pair{2560,1440},std::pair{3840,2160}}){
        if(std::abs(double(size.first)*height-double(size.second)*width)<=std::max(width,height))add(size.first,size.second);
    }
    std::sort(sizes.begin(),sizes.end());sizes.erase(std::unique(sizes.begin(),sizes.end()),sizes.end());return sizes;
}
std::pair<unsigned,unsigned> scene_resolution(const AppConfig& config,unsigned width,unsigned height){
    std::tie(width,height)=presentation_resolution(config,width,height);
    if(config.mode!=WindowMode::borderless||!width||!height)return {width,height};
    // Uniformly scale within the chosen size, keeping the selected camera
    // aspect. Legacy settings and a move to a smaller display cannot stretch
    // the scene or cause an implicit render resolution above the output size.
    const double scale=std::min({1.0,double(config.width)/width,double(config.height)/height});
    return {unsigned(std::max(1.0,std::round(width*scale))),unsigned(std::max(1.0,std::round(height*scale)))};
}
AppConfig load_app_config(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path)) {
        write_app_config(path,AppConfig{});
    }
    std::ifstream input(path);
    if(!input)throw std::runtime_error("Could not open user/config.ini");
    return parse_app_config(input);
}
void configure_console(bool debug) {
#ifdef _WIN32
    // Detach this process only; never hide a terminal shared with the caller.
    if(!debug)FreeConsole();
    else if(!GetConsoleWindow() && AllocConsole()) {
        FILE* stream=nullptr;
        freopen_s(&stream,"CONOUT$","w",stdout);
        freopen_s(&stream,"CONOUT$","w",stderr);
        freopen_s(&stream,"CONIN$","r",stdin);
    }
#else
    (void)debug;
#endif
}
SDL_Window* create_game_window(const char* title,bool hidden) {
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON,"101");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,"101");
    const auto& config=app_config();
    auto* window=SDL_CreateWindow(title,config.width,config.height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIDDEN);
    checked(window!=nullptr);
    try {
        // Capture tools stay hidden/windowed and must not change the desktop mode.
        if(!hidden && config.mode!=WindowMode::windowed) {
            if(config.mode==WindowMode::fullscreen) {
                SDL_DisplayMode mode{};
                checked(SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),config.width,config.height,0,false,&mode));
                if(mode.w!=config.width || mode.h!=config.height)
                    throw std::runtime_error("Configured fullscreen resolution is not supported by this display. Change user/config.ini.");
                checked(SDL_SetWindowFullscreenMode(window,&mode));
            } else checked(SDL_SetWindowFullscreenMode(window,nullptr));
            checked(SDL_SetWindowFullscreen(window,true));
            checked(SDL_SyncWindow(window));
        }
        if(!hidden)checked(SDL_ShowWindow(window));
        return window;
    } catch(...) {SDL_DestroyWindow(window);throw;}
}
}
