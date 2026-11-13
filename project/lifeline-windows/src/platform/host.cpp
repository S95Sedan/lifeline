#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "app/config.hpp"
#include "platform/host.hpp"
#include <cstdio>
namespace lifeline {
std::pair<int,int> display_resolution_limit(SDL_DisplayID display){
    const auto* desktop=display?SDL_GetDesktopDisplayMode(display):nullptr;
    std::pair fallback=desktop?std::pair{desktop->w,desktop->h}:std::pair{0,0};

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

    return fallback;
}
void configure_console(bool debug) {

    // Detach this process only; never hide a terminal shared with the caller.
    if(!debug)FreeConsole();
    else if(!GetConsoleWindow() && AllocConsole()) {
        FILE* stream=nullptr;
        freopen_s(&stream,"CONOUT$","w",stdout);
        freopen_s(&stream,"CONOUT$","w",stderr);
        freopen_s(&stream,"CONIN$","r",stdin);
    }

}
}
namespace lifeline::platform {
const Profile& profile(){static constexpr unsigned scales[]{50,75,100,125,150,200};static const Profile p{"d3d12",1280,960,false,200,scales};return p;}
std::string normalize_renderer(std::string_view value){return std::string(value);}
void replace_file(const std::filesystem::path& from,const std::filesystem::path& to){if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::filesystem::filesystem_error("Replace file",from,to,std::error_code(GetLastError(),std::system_category()));}
bool local_time(std::time_t value,std::tm& result){return localtime_s(&result,&value)==0;}
void configure_window_icons(){SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON,"101");SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,"101");}
void sync_window(SDL_Window* window){if(!SDL_SyncWindow(window))throw std::runtime_error(SDL_GetError());}
}
