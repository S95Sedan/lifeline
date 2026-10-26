#pragma once
#include <filesystem>
#include <istream>
#include <string>
#include <utility>
#include <vector>
#include <SDL3/SDL_video.h>
#include "rendering/quality.hpp"
#include "media/audio_settings.hpp"
#include "platform/audio_devices.hpp"

namespace lifeline {
enum class WindowMode { windowed, borderless, fullscreen };
enum class AspectRatio { automatic, standard, widescreen };
struct AppConfig {
    bool debug = false;
    std::string renderer = "d3d12";
    int width = 1280;
    int height = 960;
    WindowMode mode = WindowMode::borderless;
    AspectRatio aspect_ratio = AspectRatio::standard;
    bool vsync=true;
    unsigned render_scale=100,texture_filter=0; // 0: authored, 1: nearest, 2: linear.
    GraphicsQuality quality;
    AudioVolumes audio;
    AudioDeviceSettings devices;
    bool operator==(const AppConfig&) const=default;
};
AppConfig parse_app_config(std::istream& input);
AppConfig load_app_config(const std::filesystem::path& path);
AppConfig& app_config();
std::filesystem::path user_config_path();
void write_app_config(const std::filesystem::path&,const AppConfig&);
unsigned graphics_preset(const AppConfig&); // Low, Medium, High, Ultra, Custom.
void apply_graphics_preset(AppConfig&,unsigned preset);
void apply_display_settings(SDL_Window*,const AppConfig&,bool hidden=false);
// Monitor's preferred physical size; fall back to the desktop mode when unavailable.
std::pair<int,int> display_resolution_limit(SDL_DisplayID);
const char* aspect_ratio_name(AspectRatio);
std::pair<unsigned,unsigned> presentation_resolution(const AppConfig&,unsigned output_width,unsigned output_height);
std::vector<std::pair<int,int>> borderless_render_resolutions(int output_width,int output_height,AspectRatio=AspectRatio::automatic);
std::pair<unsigned,unsigned> scene_resolution(const AppConfig&,unsigned output_width,unsigned output_height);
void configure_console(bool debug);
SDL_Window* create_game_window(const char* title, bool hidden);
}
