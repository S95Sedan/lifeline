#pragma once
#include <ctime>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
struct SDL_Window;
namespace lifeline {
class SceneRenderer;
enum class RendererBackend;
namespace platform {
struct Profile {
    const char* renderer;
    int width,height;
    bool automatic_aspect;
    unsigned max_render_scale;
    std::span<const unsigned> render_scales;
    // Compositor-managed fullscreen keeps the display mode and scales the scene.
    bool fullscreen_render_resolution=false;
    bool fullscreen_only=false;
};
const Profile& profile();
std::string normalize_renderer(std::string_view);
void replace_file(const std::filesystem::path&,const std::filesystem::path&);
bool local_time(std::time_t,std::tm&);
void configure_window_icons();
void sync_window(SDL_Window*);
std::unique_ptr<SceneRenderer> make_renderer(RendererBackend);
}
}
