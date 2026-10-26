#pragma once
#include "assets/flash.hpp"
#include <array>
#include <filesystem>
#include "platform/bindings.hpp"
#include "app/config.hpp"

namespace lifeline {
struct MenuSettings {
    unsigned controls=0,sound=0;
    int screen_x=0,screen_y=0;
    InputBindings bindings;
    bool operator==(const MenuSettings&) const = default;
};
MenuSettings read_menu_settings(const std::filesystem::path&);
void write_menu_settings(const std::filesystem::path&,const MenuSettings&);
struct OptionsAssets {
    std::map<std::string,FlashMovie> movies;
    std::array<std::string,6> help;
    explicit OptionsAssets(std::span<const unsigned char> script);
};
class OptionsController {
public:
    enum class Page {main,controls,display,graphics,sound,microphone,closed};
    enum class Layout {full_screen,side_panel};
private:
    const OptionsAssets* assets_;
    MenuSettings* settings_;
    Page page_=Page::main;
    unsigned row_=0,first_row_=0,main_row_=0;
    bool capturing_=false;
    unsigned column_=0;
    Layout layout_=Layout::full_screen;
    AppConfig graphics_,pending_graphics_;
    SDL_DisplayID display_=0;
    std::pair<int,int> output_resolution_{0,0};
    std::vector<std::pair<int,int>> resolutions_,borderless_resolutions_,fullscreen_resolutions_;
    FlashMovie ui_;
    unsigned body_font_=0,caption_font_=0;
    std::string notice_;
    std::optional<AudioBus> audio_preview_;
    std::vector<AudioDeviceChoice> playback_devices_,microphone_devices_;
    unsigned microphone_level_=0;
    bool microphone_clipping_=false,microphone_listening_=false;
    std::string microphone_error_;
    float settings_row_y(unsigned)const;
    float settings_row_height()const;
    void refresh_audio_devices();
    std::unique_ptr<FlashPlayer> root_,dialog_;
    void open_main();
    void refresh();
    void refresh_panel();
    struct SettingsRow {std::string label,value,help;};
    std::vector<SettingsRow> settings_rows()const;
    void open(Page);
    bool select_fullscreen_resolution();
    void select_window_resolution();
    unsigned row_count() const;
    void reveal_selection();
public:
    OptionsController(const OptionsAssets&,MenuSettings&,AppConfig graphics={},SDL_DisplayID display=0,Layout layout=Layout::full_screen);
    void advance();
    void display(SDL_DisplayID);
    void move(int horizontal,int vertical);
    void confirm();
    void back();
    bool event(const SDL_Event&,SDL_JoystickID selected);
    void pointer(float x,float y,bool click);
    bool capturing()const{return capturing_;}
    unsigned column()const{return column_;}
    unsigned first_visible_row()const{return first_row_;}
    static constexpr unsigned visible_rows=6;
    const AppConfig& graphics()const{return graphics_;}
    const AppConfig& graphics_preview()const{return pending_graphics_;}
    // Commit only after the frontend successfully applies the requested values.
    void graphics_applied();
    void display_failed(std::string message);
    void audio_preview_failed(const std::string& message);
    void audio_devices_failed(const AudioDeviceSettings& previous,const std::string& message);
    void microphone_meter(unsigned level,bool clipping,bool listening,const std::string& error={});
    unsigned microphone_level()const{return microphone_level_;}
    bool microphone_listening()const{return microphone_listening_;}
    Page page() const {return page_;}
    unsigned selection() const {return row_;}
    bool ready() const {return page_!=Page::closed;}
    std::optional<AudioBus> take_audio_preview(){const auto value=audio_preview_;audio_preview_.reset();return value;}
    const FlashPlayer& root() const {return *root_;}
    const FlashPlayer* dialog() const {return dialog_.get();}
    MenuSettings preview() const {return *settings_;}
};
struct SaveMenuAssets {
    FlashMovie window,title,input;
    SaveMenuAssets(std::span<const unsigned char> window_data,std::span<const unsigned char> title_data,std::span<const unsigned char> input_data)
        :window(window_data,"systemwindow.cf"),title(title_data,"loadtitle.cf"),input(input_data,"inputinfo.cf") {}
};
// The current port cannot produce native saves yet. Preserve the original
// empty-save and Bonus prerequisite paths, without inventing successful loads.
class EmptySaveController {
public:
    enum class Phase {notice,question,returning,closing,closed};
private:
    FlashPlayer window_,title_,input_;
    bool bonus_=false,yes_=false;
    Phase phase_=Phase::notice;
    void notice();
public:
    EmptySaveController(const SaveMenuAssets&,bool bonus);
    void advance();
    void move(int vertical);
    void confirm();
    void back();
    const FlashPlayer& window() const {return window_;}
    const FlashPlayer& title() const {return title_;}
    const FlashPlayer& input() const {return input_;}
    Phase phase() const {return phase_;}
    bool yes() const {return yes_;}
};

}
