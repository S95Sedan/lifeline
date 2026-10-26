#include "ui/navigator.hpp"
#include "script/memory.hpp"
#include <algorithm>
#include <execution>
#include <numeric>
namespace lifeline {
std::shared_ptr<const AssetImage> Navigator::image(const std::string& name){
    // The original member lookup walks loaded archive slots in load order.
    // Room archives intentionally repeat the shared map ring, so requiring a
    // globally unique member makes an ordinary room transition ambiguous.
    const auto* files=resources_.first_loaded_files(name);if(!files)return {};
    for(const auto& file:*files)if(file.conversion=="rgba-bitmap-v1"||file.conversion=="map-bitmap-v1"){
        const auto& b=*file.bytes;
        if(b.size()<122||b[0]!='B'||b[1]!='M'||le16(b.data()+28)!=32||le32(b.data()+30)!=3)throw std::runtime_error("Invalid installed map bitmap: "+name);
        const unsigned w=le32(b.data()+18),h=le32(b.data()+22),offset=le32(b.data()+10);
        if(!w||!h||w>4096||h>4096||std::uint64_t(offset)+std::uint64_t(w)*h*4>b.size())throw std::runtime_error("Invalid map extent: "+name);
        auto out=std::make_shared<AssetImage>(AssetImage{name,w,h,Bytes(std::size_t(w)*h*4)});
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* d=out->rgba.data()+(y*w+x)*4;const auto* s=b.data()+offset+((h-y-1)*w+x)*4;d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=s[3];}return out;
    }
    throw std::runtime_error("Map resource needs installed bitmap conversion: "+name);
}
void Navigator::load(const script::Invocation& c){
    auto next=images_;
    constexpr unsigned slots[]{0,1,5,6,7,8};
    for(unsigned i=0;i<6;++i){const auto name=script::read_script_string(c,std::trunc(c.arguments[i])*4.f);next[slots[i]]=name.empty()?nullptr:image(name);}
    for(unsigned i=0;i<3;++i)next[i+2]=image("map_0"+std::to_string(i+1)+".raw");
    images_=std::move(next);
    continuation_.fill({});
    // The guest-ring hall has two authored map sheets. The escape worker
    // continues beyond sheet one's bounds without executing the other
    // branch's 1028 (entry_068 681ec). Retain that original continuation,
    // rather than plotting the second section beyond the first sheet.
    if(images_[0]&&images_[0]->name=="67rouka_02.raw"){
        continuation_[0]=image("67rouka_04.raw");
        continuation_[1]=image("67rouka_03.raw");
    }
}
void Navigator::layout(std::span<const float> values){
    for(float v:values)if(!std::isfinite(v))throw std::runtime_error("Invalid map layout");
    for(unsigned i=0;i<4;++i)if(values[i]<=0||values[i]>4096)throw std::runtime_error("Invalid map dimensions");
    if(values[6]==0)throw std::runtime_error("Invalid map world scale");
    std::copy_n(values.begin(),7,layout_.begin());
}
void Navigator::visible(bool enabled){
    if(enabled){pulse_=.3f;pulse_step_=.025f;state_=2;active_=true;}else state_=3;
}
void Navigator::message(int code){
    if(code==34003){queue_.clear();opacity_=timer_=0;pulse_=.3f;pulse_step_=.025f;state_=0;active_=false;return;}
    if(code==34000||code==34001){if(queue_.size()<16)queue_.push_back(code);}
    // 1665b8 ignores input and the other command-manager broadcasts.
}
bool Navigator::advance(){
    while(!queue_.empty()){visible(queue_.front()==34000);queue_.pop_front();}
    if(state_==0)opacity_=0;
    else if(state_==1)opacity_=1;
    else if(state_==2){opacity_+=.1f;if(opacity_>=1){opacity_=1;state_=1;active_=true;post_(34007,5,1);}}
    else if(state_==3){opacity_-=.1f;if(opacity_<=0){opacity_=0;state_=0;active_=false;}}
    if(state_){pulse_+=pulse_step_;if(pulse_>=1){pulse_=1;pulse_step_=-.025f;}if(pulse_<=.3f){pulse_=.3f;pulse_step_=.025f;}}
    timer_+=1;if(timer_>10)timer_=0;
    return active_;
}
script::Bindings Navigator::bindings(){
    using namespace script;Bindings result;
    result.emplace(1028,Binding{13,0,[this](const Invocation& c){layout(c.arguments.subspan(6));load(c);message(34003);return Reply{false,0};}});
    result.emplace(1029,Binding{7,0,[this](const Invocation& c){layout(c.arguments);return Reply{false,0};}});
    result.emplace(1030,Binding{6,0,[this](const Invocation& c){load(c);return Reply{false,0};}});
    result.emplace(1031,Binding{0,0,[this](const Invocation&){images_.fill({});continuation_.fill({});opacity_=0;visible(false);return Reply{false,0};}});
    result.emplace(1032,Binding{1,0,[this](const Invocation& c){visible(c.arguments[0]!=0);return Reply{false,0};}});
    result.emplace(1033,Binding{0,0,[this](const Invocation&){return Reply{false,float(state_)};}});
    return result;
}
void Navigator::render(AssetImage& target,math::Vector4 actor)const{
    if(!state_)return;
    if(continuation_[0]&&continuation_[1]&&actor.z>layout_[5]+layout_[1]*layout_[6]/10.f){
        auto next=*this;next.images_[0]=continuation_[0];next.images_[1]=continuation_[1];next.continuation_.fill({});
        // World mapping from 681ec; use the complete installed label sheet,
        // not that unused branch's smaller label crop.
        next.layout_={109,350,float(continuation_[1]->width),float(continuation_[1]->height),-150,1088.600341796875f,30};
        next.render(target,actor);return;
    }
    for(unsigned i:{0u,1u,2u,3u,4u,5u,6u,8u})if(!images_[i])return;
    if(layout_[0]<=0||layout_[1]<=0||layout_[6]==0)throw std::runtime_error("Map layout has not been initialized");
    const float scale=frame_scale(target);
    const float offset_x=(float(target.width)-640.f*scale)*.5f;
    const float offset_y=(float(target.height)-448.f*scale)*.5f;
    std::vector<int> rows(target.height);std::iota(rows.begin(),rows.end(),0);
    static const auto brightness=[](){std::array<float,256> values{};for(unsigned i=0;i<values.size();++i)values[i]=float(i*76/128);return values;}();
    const auto sprite=[&](unsigned index,float cx,float cy,float w,float h,float alpha){
        const auto& source=images_[index];if(!source)return;
        // A display rectangle may crop the texture or extend beyond its last
        // stored scanline (the authored 80S2rouka map is one row shorter).
        // Only stored texels contribute to the overlay.
        const float left=cx-w*.5f+offset_x/scale,top=cy-h*.5f+offset_y/scale;
        const int x0=std::max(0,int(std::ceil(left*scale))),x1=std::min(int(target.width),int(std::ceil((left+w)*scale)));
        const int y0=std::max(0,int(std::ceil(top*scale))),y1=std::min(int(target.height),int(std::ceil((top+h)*scale)));
        if(x0>=x1||y0>=y1)return;
        std::vector<unsigned> columns;columns.reserve(x1-x0);for(int x=x0;x<x1;++x)columns.push_back(unsigned(std::clamp(float(x)/scale-left,0.f,w-1)));
        std::array<float,256> opacity;for(unsigned i=0;i<opacity.size();++i)opacity[i]=std::clamp(float(i)/128.f*alpha,0.f,1.f);
        const auto raster_row=[&](int y){const auto sy=unsigned(std::clamp(float(y)/scale-top,0.f,h-1));if(sy>=source->height)return;
            const auto* input=source->rgba.data()+std::size_t(sy)*source->width*4;auto* output=target.rgba.data()+std::size_t(y)*target.width*4;
            for(int x=x0;x<x1;++x){const auto sx=columns[x-x0];if(sx>=source->width)continue;const auto* s=input+sx*4;const float a=opacity[s[3]];if(a==0)continue;auto* d=output+x*4;
                for(unsigned k=0;k<3;++k)d[k]=static_cast<unsigned char>(std::clamp(float(d[k])+(brightness[s[k]]-d[k])*a,0.f,255.f));
            }
        };
        // Finish each layer before the next; independent rows keep overlap and
        // byte-domain blending identical while sharing the work across cores.
        if(std::size_t(x1-x0)*(y1-y0)>=65536)std::for_each(std::execution::par,rows.begin()+y0,rows.begin()+y1,raster_row);
        else for(int y=y0;y<y1;++y)raster_row(y);
    };
    sprite(0,402,252,layout_[0],layout_[1],opacity_*.5f);
    sprite(1,402,252,std::trunc(layout_[2]),std::trunc(layout_[3]),opacity_*.5f);
    // Eight-sector additive marker, centered on Rio's authored world position.
    const float px=402-layout_[0]*.5f+(actor.x-layout_[4])*10/layout_[6]+offset_x/scale,py=252-layout_[1]*.5f+(actor.z-layout_[5])*10/layout_[6]+offset_y/scale;
    const float center_alpha=(timer_/10*.5f+.5f)*opacity_;
    for(int y=std::max(0,int(std::floor((py-10)*scale)));y<std::min(int(target.height),int(std::ceil((py+10)*scale)));++y)for(int x=std::max(0,int(std::floor((px-10)*scale)));x<std::min(int(target.width),int(std::ceil((px+10)*scale)));++x){
        const float dx=std::abs(float(x)/scale-px),dy=std::abs(float(y)/scale-py),radius=std::max(dx,dy)+.41421356237f*std::min(dx,dy);
        if(radius>=10)continue;const float a=(1-radius/10)*center_alpha;auto* d=target.rgba.data()+(y*target.width+x)*4;
        for(unsigned k=0;k<3;++k){const float color=128.f+(k==2?(25.f-128.f)*(radius/10):0.f);d[k]=static_cast<unsigned char>(std::min(255.f,d[k]+color*a));}
    }
    sprite(2,106,249,140,367,opacity_);sprite(3,427,16,412,19,opacity_);sprite(4,498,45,272,29,opacity_);
    sprite(5,536,45,191,25,opacity_*.5f);sprite(7,89,159,94,92,opacity_*.5f);sprite(6,89,159,94,92,opacity_*.5f*pulse_);sprite(8,106,304,109,103,opacity_*.5f);
}
}
