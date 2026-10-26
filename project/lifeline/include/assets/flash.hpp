#pragma once
#include "rendering/image.hpp"
#include "core/math.hpp"
#include <map>
#include <variant>
#include <memory>
#include <functional>
#include <optional>

namespace lifeline {
// Recovered CAppFlashScript behavior expressed as native scene data. No EE,
// VU, GIF or GS machine state participates in this player.
struct FlashFill {
    bool textured = false;
    unsigned texture = 0;
    float origin_x = 0, origin_y = 0;
    math::Vector4 color{1,1,1,1};
    std::vector<std::vector<math::Vector4>> polygons;
};
struct FlashShape {
    math::Vector4 bounds{}; // xmin, ymin, xmax, ymax from the exported shape.
    std::vector<FlashFill> fills;
};
struct FlashFont {
    unsigned columns=0;
    float u_step=0,v_step=0;
    std::vector<unsigned> characters;
    std::vector<float> advances;
};
struct FlashGlyph { unsigned font,index;float x,y,width,height;math::Vector4 color;bool dynamic=false; };
enum class FlashTextAlignment { left, center };
struct FlashTextValue {
    std::string value;float width=32,height=32;math::Vector4 color{1,1,1,1};FlashTextAlignment alignment=FlashTextAlignment::left;
    std::int16_t flags=1,line_spacing=0;
    std::int32_t horizontal_spacing=0;
    float horizontal_offset=0;
};
struct FlashText {
    std::string variable;
    bool hidden=false;
    math::Vector4 bounds{};
    unsigned font=0;
    float height=0;
    std::vector<FlashGlyph> glyphs;
};
struct FlashPlacement {
    unsigned depth=0, shape=0, flags=0, clip_depth=0;
    unsigned button_state=1;
    math::Matrix4 matrix=math::identity();
    math::Vector4 multiply{1,1,1,1}, add{};
};
struct FlashRemove { unsigned depth; };
struct FlashBackground { math::Vector4 color; };
struct FlashAction {
    enum Kind { play, stop, go_to_frame, set_variable, bytecode } kind;
    unsigned frame=0;
    std::string name, value;
    Bytes program;
};
using FlashCommand=std::variant<FlashPlacement,FlashRemove,FlashBackground,FlashAction>;
struct FlashFrame { std::vector<FlashCommand> commands; };
struct FlashButtonPart {
    unsigned resource=0, states=0;
    math::Matrix4 matrix=math::identity();
    math::Vector4 color{1,1,1,1};
};
struct FlashButton {
    std::vector<FlashButtonPart> parts;
    unsigned condition=0;
    FlashFrame action;
    std::vector<std::pair<unsigned,FlashFrame>> linked_actions;
};
struct FlashVertex { float x,y,u,v; math::Vector4 color; };
struct FlashDraw { bool textured=false; unsigned texture=0; std::vector<FlashVertex> vertices; std::vector<int> indices; };
class FlashMovie {
public:
    std::string name;
    unsigned width=0,height=0,rate=0;
    std::map<unsigned,AssetImage> textures;
    std::map<unsigned,FlashShape> shapes;
    std::vector<FlashFrame> frames;
    std::map<unsigned,std::vector<FlashFrame>> timelines;
    std::map<unsigned,FlashButton> buttons;
    std::map<unsigned,FlashFont> fonts;
    std::map<unsigned,FlashText> texts;
    std::optional<unsigned> input_prompt_font;
    FlashMovie(std::span<const unsigned char>,std::string name);
};
class FlashPlayer {
    using EventSink=std::function<void(unsigned,int)>;
    struct EventState {EventSink sink;unsigned suppressed=0;bool input_enabled=true;};
    std::shared_ptr<EventState> event_sink_=std::make_shared<EventState>();
    const FlashMovie* movie_;
    const std::vector<FlashFrame>* frames_;
    bool nested_=false;
    std::map<unsigned,std::unique_ptr<FlashPlayer>> children_;
    std::map<unsigned,FlashPlacement> layers_;
    std::map<std::string,std::string> variables_;
    // Dynamic text belongs to the loaded CF instance, including nested
    // button timelines such as the save selector's Details panel.
    using TextValues=std::map<std::string,FlashTextValue>;
    std::shared_ptr<TextValues> text_values_=std::make_shared<TextValues>();
    math::Vector4 background_{0,0,0,1};
    unsigned frame_=0;
    int jump_=-1, jump_playing_=-1;
    bool playing_=true,finished_=false,looping_=false;
    void apply(unsigned);
    void execute(const FlashAction&);
    void execute_bytecode(std::span<const unsigned char>);
    void variable(std::string name,std::string value,std::optional<float> number={});
    void sync_children(bool tick);
    FlashPlayer(const FlashMovie& movie,const std::vector<FlashFrame>& frames,std::shared_ptr<EventState> sink,std::shared_ptr<TextValues> texts):event_sink_(std::move(sink)),movie_(&movie),frames_(&frames),nested_(true),text_values_(std::move(texts)) { seek(0); }
public:
    explicit FlashPlayer(const FlashMovie& movie):movie_(&movie),frames_(&movie.frames) { seek(0); }
    FlashPlayer(const FlashPlayer&)=delete;
    FlashPlayer& operator=(const FlashPlayer&)=delete;
    FlashPlayer(FlashPlayer&&) noexcept=default;
    FlashPlayer& operator=(FlashPlayer&&) noexcept=default;
    void seek(unsigned);
    void advance();
    void looping(bool enabled){looping_=enabled;}
    void on_event(EventSink sink){event_sink_->sink=std::move(sink);}
    void set_button_state(unsigned depth,unsigned state);
    void pointer(float x,float y,bool down,const math::Matrix4& parent=math::identity());
    void click(float x,float y);
    void set_text(std::string name,std::string value,float width=32,float height=32,math::Vector4 color={1,1,1,1},FlashTextAlignment alignment=FlashTextAlignment::left) {
        if(width<=0 || height<=0 || width>256 || height>256) throw std::runtime_error("Invalid CF text size");
        text_values_->insert_or_assign(std::move(name),FlashTextValue{std::move(value),width,height,color,alignment});
    }
    void text_content(const std::string& name,std::string value){(*text_values_)[name].value=std::move(value);}
    void text_color(const std::string& name,math::Vector4 color){(*text_values_)[name].color=color;}
    void text_property(const std::string& name,int selector,std::int32_t value);
    std::vector<FlashDraw> compose() const;
    const FlashMovie& movie() const { return *movie_; }
    const auto& layers() const { return layers_; }
    const auto& variables() const { return variables_; }
    const auto& text_values() const { return *text_values_; }
    math::Vector4 background() const { return background_; }
    unsigned frame() const { return frame_; }
    bool finished() const { return finished_; }
    bool playing() const { return playing_; }
    unsigned playback_state() const {return finished_?4u:playing_?1u:2u;}
    void rewind_stopped(){seek(0);playing_=false;jump_=-1;jump_playing_=-1;}
};
}
