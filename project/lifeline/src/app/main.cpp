#include "assets/asset_directory.hpp"
#include "assets/resources.hpp"
#include "assets/installed_paths.hpp"
#include <SDL3/SDL_filesystem.h>
#include "app/frontend.hpp"
#include "ui/input_prompts.hpp"
#include "app/config.hpp"
#include <SDL3/SDL_messagebox.h>
#include "app/speech_diagnostic.hpp"
#include "rendering/renderer.hpp"
#include "speech/backend.hpp"
#include "media/audio_output.hpp"
#include <SDL3/SDL_stdinc.h>
#include <filesystem>
#include <optional>

using namespace lifeline;

static std::vector<AssetImage> load_assets(const AssetDirectory& assets) {
    return {decode_cf_texture(assets.read(installed::konami_logo),"Konami logo"),
            decode_cf_texture(assets.read(installed::sony_logo),"Sony presentation"),
            decode_cf_texture(assets.read("ui/title/tit_bg.cf"),"Title background texture")};
}

int main(int argc, char** argv) {
    try {
        const auto* executable_base=SDL_GetBasePath();
        if(!executable_base)throw std::runtime_error("Cannot locate application directory");
        const std::string base_text(executable_base);
        app_config()=load_app_config(user_config_path());
        set_audio_volumes(app_config().audio);
        set_audio_device_settings(app_config().devices);
        const auto menu_settings=read_menu_settings(user_config_path().parent_path()/"menu-settings.txt");input_bindings()=menu_settings.bindings;mono_output=menu_settings.sound==1;
        set_scene_quality(app_config().render_scale,app_config().texture_filter,app_config().quality);
        configure_console(app_config().debug);
        select_renderer_backend(app_config().renderer);
        if(const auto* backend=SDL_getenv("LIFELINE_RENDERER"))select_renderer_backend(backend);
        const bool explicit_source=argc>1 && !std::string(argv[1]).starts_with("--");
        std::filesystem::path source;
        if(explicit_source) source=argv[1];
        else {
            const auto* base=SDL_GetBasePath();if(!base) throw std::runtime_error("Cannot locate application data directory");
            const std::string text(base);source=std::filesystem::path(std::u8string(text.begin(),text.end()))/"data";
        }
        auto speech_configuration=speech_backend_configuration();
        bool verify_data=false;
        bool verify = false;
        bool preview = false;
        std::string speech_diagnostic;
        std::string capture;
        bool presentation=false,tutorial_intro=false,vent_speech_test=false,door_speech_test=false,bomb_speech_test=false;
        std::string movie_name,movie_capture,cinematic_name;
        unsigned movie_frame=0;
        bool movie_frame_set=false;
        unsigned image = 0;
        for (int i = explicit_source?2:1; i < argc; ++i) {
            const std::string option = argv[i];
            if(option=="--data" && i+1<argc) {source=argv[++i];}
            else if(option=="--renderer" && i+1<argc)select_renderer_backend(argv[++i]);
            else if(option=="--speech-test" && i+1<argc)speech_diagnostic=argv[++i];
            else if(option=="--vent-speech-test"){speech_diagnostic=(source.parent_path().parent_path()/"dev/vent-speech-captures").string();vent_speech_test=true;}
            else if(option=="--door-speech-test"){speech_diagnostic=(source.parent_path().parent_path()/"dev/door-speech-captures").string();vent_speech_test=true;door_speech_test=true;}
            else if(option=="--bomb-speech-test"){speech_diagnostic=(source.parent_path().parent_path()/"dev/bomb-speech-captures").string();vent_speech_test=true;bomb_speech_test=true;}
            else if(option=="--verify-data") verify_data=true;
            else if(option=="--tutorial-intro") tutorial_intro=true;
            else if(option=="--script-tutorial"){tutorial_intro=true;}
            else if (option == "--presentation") presentation=true;
            else if(option=="--cinematic" && i+1<argc) cinematic_name=argv[++i];
            else if (option == "--movie" && i+1<argc) movie_name=argv[++i];
            else if (option == "--movie-capture" && i+1<argc) movie_capture=argv[++i];
            else if (option == "--movie-frame" && i+1<argc) {
                const std::string value=argv[++i];std::size_t parsed=0;
                if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("Invalid movie frame");
                const auto number=std::stoul(value,&parsed);
                if (parsed!=value.size() || number>100000) throw std::runtime_error("Movie frame exceeds limit");
                movie_frame=static_cast<unsigned>(number);
                movie_frame_set=true;
            }
            else if (option == "--verify-assets") verify = true;
            else if (option == "--preview") preview = true;
            else if (option == "--capture" && i + 1 < argc) capture = argv[++i];
            else if (option == "--image" && i + 1 < argc) {
                const std::string value = argv[++i];
                if (value.size() != 1 || value[0] < '0' || value[0] > '2') throw std::runtime_error("Image must be 0, 1 or 2");
                image = static_cast<unsigned>(value[0] - '0');
            } else throw std::runtime_error("Unknown or incomplete option: " + option);
        }
        speech_backend_configuration(std::move(speech_configuration));
        if(!std::filesystem::is_directory(source)) throw std::runtime_error("The game requires an installed data directory. Install the disc data before launching.");
        if(!speech_diagnostic.empty()){
            if(tutorial_intro || presentation || !movie_name.empty() || !cinematic_name.empty() || verify_data || verify || preview || !capture.empty() || movie_frame_set)throw std::runtime_error("Choose microphone diagnostics or another mode");
            show_speech_diagnostic(AssetDirectory(source),speech_diagnostic,movie_capture,vent_speech_test,door_speech_test,bomb_speech_test);return 0;
        }
        if(tutorial_intro) {
            if(presentation || !movie_name.empty() || !cinematic_name.empty() || verify_data || verify || preview || !capture.empty())throw std::runtime_error("Choose Tutorial instructions or another mode");
            AssetDirectory data(source);show_tutorial_intro(data,movie_capture,movie_frame);return 0;
        }
        if(!cinematic_name.empty()) {
            if(presentation || !movie_name.empty() || verify_data || verify || preview || !capture.empty()) throw std::runtime_error("Choose an external cinematic or another mode");
            AssetDirectory data(source);show_cinematic(data,cinematic_name,movie_capture,movie_frame);return 0;
        }
        if(verify_data) {
            if(presentation || !movie_name.empty() || verify || preview || !capture.empty() || !movie_capture.empty() || movie_frame_set)
                throw std::runtime_error("Choose data verification or a display mode");
            AssetDirectory assets(source);std::cout<<"Readable external asset files: "<<assets.verify_readable()<<'\n';return 0;
        }
        if(!presentation && movie_name.empty() && !verify && !preview && capture.empty()) presentation=true;
        if (presentation || !movie_name.empty()) {
            if (verify || preview || !capture.empty() || (presentation && !movie_name.empty()))
                throw std::runtime_error("Choose one execution mode");
            auto assets=std::make_unique<AssetDirectory>(source);
            Resources resources(*assets);
            const auto read_member=[&](const std::string& archive,const std::string& member) {
                return resources.read_raw(archive,member);
            };
            std::vector<FlashMovie> movies;
            const auto load=[&](const char* path,const char* member) {
                auto& movie=movies.emplace_back(read_member(path,member),member);
                if(movie.name=="tit_load.cf")prepare_title_prompt(movie,FlashMovie(read_member(path,"systemwindow.cf"),"systemwindow.cf"));
            };
            if (presentation || movie_name=="konami") load("/archive/sns/complogo.a","KOA_logo.cf");
            if (presentation || movie_name=="sony") load("/archive/sns/complogo.a","complogo.cf");
            if (movie_name=="title") load("/archive/teru/a/1004.a","tit_bg.cf");
            if (presentation || movie_name=="menu") load("/archive/teru/a/1004.a","tit_load.cf");
            if(movie_name=="system" || movie_name=="status" || movie_name=="loadtitle") {
                const auto member=movie_name=="system"?"systemwindow.cf":movie_name=="status"?"statuswindow.cf":"loadtitle.cf";
                load("/archive/teru/a/1004.a",member);
            }
            if(movie_name=="bonus-menu") {load("/archive/teru/a/1004.a","bg00.cf");load("/archive/teru/a/1004.a","tit_load.cf");}
            if(movie_name=="microphone") load("/archive/teru/a/1004.a","micw.cf");
            if(movie_name=="load") load("/archive/teru/a/1004.a","fileselecter.cf");
            if(movie_name=="bonus") load("/archive/teru/a/1004.a","sel00.cf");
            if(movie_name=="options") {load("/archive/teru/a/1017.a","op_title.cf");load("/archive/teru/a/1004.a","tit_load.cf");}
            if(movie_name.starts_with("op_")) load("/archive/teru/a/1017.a",(movie_name+".cf").c_str());
            if (movies.empty()) throw std::runtime_error("Unknown movie: use konami, sony, title, menu, options, op_keya/b/c, op_sound, op_screen, op_bright, op_yesno, load, bonus, bonus-menu, microphone, system, status or loadtitle");
            std::optional<BonusAssets> bonus;
            std::optional<SaveMenuAssets> saves;
            std::optional<OptionsAssets> options;
            std::optional<FlashMovie> cursor;std::optional<TitleNavigation> navigation;
            if(presentation || movie_name=="menu" || movie_name=="options" || movie_name=="bonus-menu") {
                const auto title=[&](const char* member){return read_member("/archive/teru/a/1004.a",member);};
                saves.emplace(title("systemwindow.cf"),title("loadtitle.cf"),title("inputinfo.cf"));
                prepare_navigation_prompt(saves->input,FlashMovie(read_member("/archive/teru/a/1000.a","msg.cf"),"msg.cf"));
                cursor.emplace(title("tit_loadcur.cf"),"tit_loadcur.cf");
                const auto script=read_member("/archive/teru/cs/cs1004.a","entry_1004.cb");
                navigation.emplace(script);
                if(movie_name=="bonus-menu") bonus.emplace(title("bg00.cf"),title("sel00.cf"),script);
                options.emplace(read_member("/archive/teru/cs/cs1017.a","entry_1017.cb"));
                for(const auto* name:{"op_title.cf","op_keya.cf","op_keyb.cf","op_keyc.cf","op_sound.cf","op_screen.cf","op_bright.cf","op_yesno.cf"})
                    options->movies.emplace(name,FlashMovie(read_member("/archive/teru/a/1017.a",name),name));
            }
            show_movies(movies,movie_capture,movie_frame,cursor?&*cursor:nullptr,navigation?&*navigation:nullptr,options?&*options:nullptr,saves?&*saves:nullptr,bonus?&*bonus:nullptr,assets.get());return 0;
        }
        if (!movie_capture.empty() || movie_frame_set) throw std::runtime_error("Movie options require --movie or --presentation");
        if (verify && !capture.empty()) throw std::runtime_error("Choose verification or capture mode");
        auto images=load_assets(AssetDirectory(source));
        for(const auto& asset:images)std::cout<<"Decoded "<<asset.name<<": "<<asset.width<<'x'<<asset.height<<'\n';
        if (verify) return 0;
        std::cout << "Native asset preview: Left/Right or Space changes image; Close the window to exit.\n"
                  << "Static texture inspection. Use --presentation for native animation playback.\n";
        show_assets(images, capture, image);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Lifeline",e.what(),nullptr);
        return 1;
    }
}
