#pragma once
#include "script/runtime.hpp"
#include "rendering/image.hpp"
#include <array>

namespace lifeline::script {
// Native scanline distortion driven by the authored screen-effect commands.
class ScreenEffects {
    bool enabled_=false;
    bool afterimage_enabled_=false;
    bool grain_enabled_=false;
    std::array<std::array<int,3>,2> grain_colors_{{{16,32,255},{32,48,255}}};
    float grain_amount_=.5f,grain_gain_=1;
    std::array<std::uint8_t,64*32*3> grain_{};
    void apply_grain(AssetImage&)const;
    float high_=50.f/1024,low_=5.f/1024,burst_=30,pause_=30,jitter_=300;
    float wave_=50.f/1024,wave_delay_=60,wave_jitter_=120;
    float age_=0,wave_age_=0,next_burst_=0,next_wave_=0;
    std::array<float,3> upper_{128,128,128},lower_{64,64,64};
    std::array<float,448> offsets_{};
    std::function<std::uint32_t()> random_;
    struct Fade {
        float alpha=0,step=0;int draw_alpha=0;bool hold=false;
        std::array<int,4> rectangle{0,0,640,448};
        std::array<std::uint8_t,3> color{};
    } fade_;
    void apply_fade(AssetImage&)const;
    void defaults();
    void one(unsigned,float);
    void pair(unsigned,float,float);
    void triple(unsigned,float,float,float);
public:
    explicit ScreenEffects(std::function<std::uint32_t()> random):random_(std::move(random)){}
    Bindings bindings();
    void advance(std::uint64_t frame);
    void apply(AssetImage&)const;
    bool enabled()const{return enabled_;}
    bool afterimage_enabled()const{return afterimage_enabled_;}
#ifdef LIFELINE_DEV_SCENE_PROBE
    bool debug_grain_enabled()const{return grain_enabled_;}
    bool debug_fade_enabled()const{return fade_.draw_alpha!=0;}
    void debug_apply_reference(AssetImage&)const;
    void debug_apply_grain_reference(AssetImage&)const;
    void debug_apply_fade_reference(AssetImage&)const;
#endif
};
}
