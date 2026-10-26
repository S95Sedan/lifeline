#pragma once
#include "assets/resources.hpp"
#include "rendering/image.hpp"
#include "script/runtime.hpp"
#include "core/math.hpp"
#include <array>
#include <deque>
namespace lifeline {
class Navigator {
    Resources& resources_;
    std::function<void(int,int,int)> post_;
    std::array<std::shared_ptr<const AssetImage>,9> images_;
    std::array<float,7> layout_{};
    std::array<std::shared_ptr<const AssetImage>,2> continuation_;
    float opacity_=0,pulse_=.3f,pulse_step_=.025f,timer_=0;
    int state_=0;
    bool active_=false;
    std::deque<int> queue_;
    std::shared_ptr<const AssetImage> image(const std::string&);
    void load(const script::Invocation&);
    void layout(std::span<const float>);
    void visible(bool);
public:
    void reset(){message(34003);images_.fill({});continuation_.fill({});}
    bool available()const{return bool(images_[0])&&bool(images_[1]);}
    Navigator(Resources& resources,std::function<void(int,int,int)> post):resources_(resources),post_(std::move(post)){}
    script::Bindings bindings();
    void message(int code);
    bool advance();
    void render(AssetImage&,math::Vector4 actor)const;
    int state()const{return state_;}
    bool active()const{return active_;}
#ifdef LIFELINE_DEV_SCENE_PROBE
    std::string debug_description()const{
        std::string out="map "+std::to_string(state_);
        for(auto& image:images_)out+=' '+(image?image->name:"none");
        for(float v:layout_)out+=' '+std::to_string(v);
        return out;
    }
#endif
};
}
