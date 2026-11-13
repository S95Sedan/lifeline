#include "app/config.hpp"
#include "platform/host.hpp"
namespace lifeline {
std::pair<int,int> display_resolution_limit(SDL_DisplayID display){
    const auto* desktop=display?SDL_GetDesktopDisplayMode(display):nullptr;
    std::pair fallback=desktop?std::pair{desktop->w,desktop->h}:std::pair{0,0};

    return fallback;
}
void configure_console(bool debug) {

    (void)debug;

}
}
namespace lifeline::platform {
const Profile& profile(){
#ifdef LIFELINE_STEAMDECK
    static constexpr unsigned scales[]{50,75,100};static const Profile p{"opengl",1280,800,true,100,scales,true,true};
#else
    static constexpr unsigned scales[]{50,75,100,125,150,200};static const Profile p{"opengl",1280,960,false,200,scales};
#endif
    return p;
}
std::string normalize_renderer(std::string_view value){if(profile().max_render_scale==100&&(value=="vulkan"||value=="d3d12"))return "opengl";return std::string(value);}
void replace_file(const std::filesystem::path& from,const std::filesystem::path& to){std::filesystem::rename(from,to);}
bool local_time(std::time_t value,std::tm& result){return localtime_r(&value,&result)!=nullptr;}
void configure_window_icons(){}
void sync_window(SDL_Window* window){
    if(SDL_SyncWindow(window))return;
    // Gamescope can constrain or deny a requested window size/state. A pending
    // window-manager request timing out must not terminate a running game.
    if(profile().fullscreen_render_resolution)SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,"Window change remains controlled by the compositor: %s",SDL_GetError());
    else throw std::runtime_error(SDL_GetError());
}
}
