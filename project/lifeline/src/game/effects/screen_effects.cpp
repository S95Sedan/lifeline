#include "game/effects/screen_effects.hpp"
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <execution>
#include <numeric>

namespace lifeline::script {
void ScreenEffects::defaults(){
    high_=50.f/1024;low_=5.f/1024;burst_=pause_=30;jitter_=300;wave_=50.f/1024;wave_delay_=60;wave_jitter_=120;
    upper_={128,128,128};lower_={64,64,64};
}
void ScreenEffects::one(unsigned operation,float value){
    switch(operation){
    case 2:age_=value;next_burst_=burst_+pause_+float(random_()&65535)/65535.f*jitter_;break;
    case 3:wave_=value/1024;break;
    case 5:wave_age_=value;next_wave_=wave_delay_+float(random_()&65535)/65535.f*wave_jitter_;break;
    case 11:defaults();break;
    default:throw std::runtime_error("Screen-effect scalar operation not recovered: "+std::to_string(operation));
    }
}
void ScreenEffects::pair(unsigned operation,float a,float b){
    if(operation==0){high_=a/1024;low_=b/1024;}
    else if(operation==4){wave_delay_=a;wave_jitter_=b;}
    else throw std::runtime_error("Screen-effect pair operation not recovered");
}
void ScreenEffects::triple(unsigned operation,float a,float b,float c){
    if(operation==1){burst_=a;pause_=b;jitter_=c;}
    else if(operation==7)upper_={a,b,c};else if(operation==9)lower_={a,b,c};
    else throw std::runtime_error("Screen-effect triple operation not recovered");
}
Bindings ScreenEffects::bindings(){
    Bindings result;
    result.emplace(167,Binding{9,0,[this](const Invocation& c){
        for(float value:c.arguments)if(!std::isfinite(value) || double(value)<INT32_MIN || double(value)>INT32_MAX)throw std::runtime_error("Invalid colored fade parameter");
        Fade next;const float seconds=c.arguments[0];next.alpha=seconds<=0?128.f:1.f;
        next.step=seconds==0?128.f:128.f/(seconds*60.f);next.hold=std::trunc(c.arguments[8])!=0;
        for(unsigned i=0;i<4;++i)next.rectangle[i]=int(std::trunc(c.arguments[i+1]));
        // Native packs RGB with shifts/OR, then the GS reads the low bytes.
        const auto packed=std::uint32_t(int(std::trunc(c.arguments[5]))) |
            (std::uint32_t(int(std::trunc(c.arguments[6])))<<8) |
            (std::uint32_t(int(std::trunc(c.arguments[7])))<<16);
        for(unsigned i=0;i<3;++i)next.color[i]=std::uint8_t(packed>>(i*8));
        fade_=next;return Reply{false,0.f};
    }});
    result.emplace(830,Binding{0,1,[this](const Invocation& c){
        if(c.operands[0]==6){if(!grain_enabled_){grain_colors_={{{16,32,255},{32,48,255}}};grain_amount_=.5f;grain_gain_=1;grain_enabled_=true;}return Reply{};}
        if(c.operands[0]==2){afterimage_enabled_=true;return Reply{};}
        if(c.operands[0]!=1)return Reply{};if(!enabled_){defaults();enabled_=true;}return Reply{};
    }});
    result.emplace(831,Binding{0,1,[this](const Invocation& c){
        // 133ec0 scans the eight effect-manager slots. A negative ID clears
        // every slot; an absent/non-rendered positive ID is an ordinary no-op.
        // It never rejects an ID. Scene 62 removes type 2 after its movie, and
        // scene 78 explicitly creates/removes the same zoom-afterimage type.
        if(std::int32_t(c.operands[0])<0){enabled_=afterimage_enabled_=grain_enabled_=false;return Reply{};}
        if(c.operands[0]==6)grain_enabled_=false;else if(c.operands[0]==2)afterimage_enabled_=false;else if(c.operands[0]==1)enabled_=false;
        return Reply{};
    }});
    for(const auto opcode:{832u,833u,834u,838u})result.emplace(opcode,Binding{0,opcode==832?3u:opcode==833?4u:5u,[this,opcode](const Invocation& c){
        if(c.operands[0]!=1&&c.operands[0]!=6)throw std::runtime_error("Screen effect not recovered");
        std::array<float,3> values{};const unsigned count=opcode==832?1:opcode==833?2:3;
        for(unsigned i=0;i<count;++i){
            if(opcode==838){if(!c.state)throw std::logic_error("Screen effect requires worker locals");values[i]=c.state->floats[c.operands[i+2]];}
            else values[i]=std::bit_cast<float>(c.operands[i+2]);
            if(!std::isfinite(values[i]))throw std::runtime_error("Invalid screen-effect parameter");
        }
        if(c.operands[0]==6){
            const auto op=c.operands[1];
            if(count==3&&op<2){for(unsigned i=0;i<3;++i){if(double(values[i])<INT32_MIN||double(values[i])>INT32_MAX)throw std::runtime_error("Invalid screen grain color");grain_colors_[op][i]=int(std::nearbyint(values[i]));}}
            else if(count==1&&op==2)grain_amount_=values[0];else if(count==1&&op==3)grain_gain_=values[0];
            else throw std::runtime_error("Screen grain parameter not recovered");
        }else if(count==1)one(c.operands[1],values[0]);else if(count==2)pair(c.operands[1],values[0],values[1]);else triple(c.operands[1],values[0],values[1],values[2]);return Reply{};
    }});
    return result;
}
void ScreenEffects::advance(std::uint64_t frame){
    if(grain_enabled_){
        // 1363d8 fills a 64x32 noise texture with a private LCG, seeded from
        // frame/timer state. Keep it independent from the game's effect RNG.
        std::uint32_t seed=std::uint32_t(frame);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){seed=seed*1103515245u+12345u;const unsigned noise=(seed>>16)&255;
            for(unsigned c=0;c<3;++c){const int base=grain_colors_[(y&1)?0:1][c],spread=int(std::nearbyint(base*grain_amount_));grain_[(y*64+x)*3+c]=std::uint8_t((base&255)-spread+((spread*int(noise))>>8));}}
    }
    fade_.draw_alpha=0;
    if(fade_.alpha>0){
        fade_.alpha+=fade_.step;
        const double rounded=std::trunc(double(fade_.alpha));
        if(rounded>128 && !fade_.hold)fade_.alpha=0;
        else fade_.draw_alpha=int(std::clamp(rounded,0.,128.));
    }
    if(!enabled_)return;
    float amount=low_;if(age_<burst_){const float t=(burst_-age_)/burst_;amount=t*t*(high_-low_)+low_;}
    if(++age_>next_burst_){age_=0;next_burst_=burst_+pause_+float(random_()&65535)/65535.f*jitter_;}
    const bool wave=++wave_age_>next_wave_;
    if(wave){wave_age_=0;next_wave_=wave_delay_+float(random_()&65535)/65535.f*wave_jitter_;}
    for(unsigned y=0;y<448;++y){
        const float noise=(1-float(random_()&255)/127.f)*amount;
        const auto angle=std::uint16_t(frame*10+y*100);
        offsets_[y]=noise+(wave?std::sin(float(angle)*(2*std::numbers::pi_v<float>/65536))*wave_:0);
    }
    // The original updates a noise texture even when its overlay is disabled.
    // Keep its random consumption, without allocating a redundant image asset.
    for(unsigned i=0;i<2048;++i){random_();random_();}
}
void ScreenEffects::apply(AssetImage& image)const{
    if(!enabled_){apply_grain(image);apply_fade(image);return;}
    const float scale=frame_scale(image);
    const auto source=image.rgba;
    const int period=int(1024*scale);
    const auto raster_row=[&](unsigned y){const unsigned row=unsigned(y/scale);const auto& tint=(row&2)?lower_:upper_;
        const std::array<float,3> color{std::trunc(tint[0]),std::trunc(tint[1]),std::trunc(tint[2])};
        const float offset=offsets_[std::min(447u,row)]*1024*scale;
        const auto* input=source.data()+std::size_t(y)*image.width*4;
        auto* output=image.rgba.data()+std::size_t(y)*image.width*4;
        for(unsigned x=0;x<image.width;++x){
            const float sample=float(x)+offset;const int first=int(std::floor(sample));const float mix=sample-first;
            const int left=(first%period+period)%period,right=(left+1==period?0:left+1);
            const auto* a=left<int(image.width)?input+std::size_t(left)*4:nullptr;
            const auto* b=right<int(image.width)?input+std::size_t(right)*4:nullptr;
            for(unsigned c=0;c<3;++c){
                const float value=((a?float(a[c]):0.f)*(1-mix)+(b?float(b[c]):0.f)*mix)*color[c]/128.f;
                output[x*4+c]=std::uint8_t(std::clamp(value,0.f,255.f));
            }
        }
    };
    std::vector<unsigned> rows(image.height);std::iota(rows.begin(),rows.end(),0);
    if(std::size_t(image.width)*image.height>=65536)std::for_each(std::execution::par,rows.begin(),rows.end(),raster_row);
    else for(auto y:rows)raster_row(y);
    apply_grain(image);apply_fade(image);
}
void ScreenEffects::apply_grain(AssetImage& image)const{
    if(!grain_enabled_)return;
    const float scale=frame_scale(image);
    // 1365e0 subtracts the tiled noise, then applies authored gain. Noise UVs
    // cover half a 64x32 texture in each 64x32 screen tile (two-pixel texels).
    const float gain=grain_gain_>1?1+std::min(255.f,std::nearbyint((grain_gain_-1)*128))/128:std::nearbyint(grain_gain_*128)/128;
    // The gain only depends on the byte remaining after subtraction. Keep its
    // original float/clamp/truncation, evaluated once for each possible byte.
    std::array<std::uint8_t,256> levels;
    for(unsigned value=0;value<levels.size();++value)levels[value]=std::uint8_t(std::clamp(value*gain,0.f,255.f));
    // Preserve the original division (including fractional render scales).
    // UVs depend on columns/rows, not on the individual colour channels.
    std::vector<unsigned> columns(image.width),rows(image.height);
    for(unsigned x=0;x<image.width;++x)columns[x]=(unsigned(x/(2*scale))%64)*3;
    std::iota(rows.begin(),rows.end(),0);
    const auto raster_row=[&](unsigned y){
        const auto* noise=grain_.data()+(unsigned(y/(2*scale))%32)*64*3;
        auto* pixel=image.rgba.data()+std::size_t(y)*image.width*4;
        for(unsigned x=0;x<image.width;++x,pixel+=4){
            const auto* sample=noise+columns[x];
            for(unsigned c=0;c<3;++c)pixel[c]=levels[std::max(0,int(pixel[c])-sample[c])];
        }
    };
    if(std::size_t(image.width)*image.height>=65536)std::for_each(std::execution::par,rows.begin(),rows.end(),raster_row);
    else for(auto y:rows)raster_row(y);
}
void ScreenEffects::apply_fade(AssetImage& image)const{
    if(!fade_.draw_alpha)return;
    if(image.rgba.size()!=std::size_t(image.width)*image.height*4)throw std::runtime_error("Invalid colored fade target");
    const float scale=frame_scale(image),offset_x=(image.width-640*scale)*.5f,offset_y=(image.height-448*scale)*.5f;
    const auto& r=fade_.rectangle;
    const auto boundary=[&](int value,unsigned extent,float offset){return int(std::clamp(float(value)*scale+offset,0.f,float(extent)));};
    const bool full=std::min(r[0],r[2])<=0&&std::max(r[0],r[2])>=640&&std::min(r[1],r[3])<=0&&std::max(r[1],r[3])>=448;
    const int left=full?0:boundary(std::min(r[0],r[2]),image.width,offset_x),right=full?int(image.width):boundary(std::max(r[0],r[2]),image.width,offset_x);
    const int top=full?0:boundary(std::min(r[1],r[3]),image.height,offset_y),bottom=full?int(image.height):boundary(std::max(r[1],r[3]),image.height,offset_y);
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)for(unsigned c=0;c<3;++c){
        auto& target=image.rgba[(std::size_t(y)*image.width+x)*4+c];
        const int blended=int(target)+((int(fade_.color[c])-int(target))*fade_.draw_alpha>>7);
        target=std::uint8_t(std::clamp(blended,0,255));
    }
}
}
