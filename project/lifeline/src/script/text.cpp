#include "script/text.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace lifeline::script {
namespace {
int integer(float value){if(!std::isfinite(value) || double(value)<INT32_MIN || double(value)>INT32_MAX)throw std::runtime_error("Invalid text argument");return int(std::nearbyint(value));}
State& state(const Invocation& c){if(!c.state)throw std::runtime_error("Text command requires script state");return *c.state;}
std::string string(const Invocation& c,unsigned address){
    std::string result;
    for(unsigned word=address;;++word){
        if(c.state?!c.state->data_contains(c.program,word):word>=c.program.size())throw std::runtime_error("Unterminated script text");
        auto bits=c.program.size()>word?c.program.word(word):0u;
        if(c.state){const auto it=c.state->data->find(word);if(it!=c.state->data->end())bits=std::bit_cast<unsigned>(it->second);}
        for(unsigned b=0;b<4;++b){const auto ch=char(bits>>(8*b));if(!ch)return result;result+=ch;}
    }
}
std::array<unsigned,4> color(unsigned r,unsigned g,unsigned b,unsigned a){const auto packed=r|(g<<8)|(b<<16)|(a<<24);return {packed&255,(packed>>8)&255,(packed>>16)&255,packed>>24};}
std::string digits(unsigned value){std::string result;for(unsigned divisor=10000;divisor;divisor/=10){result+=char(0x82);result+=char(0x4f+value/divisor);value%=divisor;}return result;}
}
int Text::allocate(int mode){for(unsigned i=0;i<windows_.size();++i)if(windows_[i].mode==-1){auto buffer=std::move(windows_[i].buffer);windows_[i]=Window{};windows_[i].buffer=std::move(buffer);windows_[i].mode=mode;return int(i);}return -1;}
Text::Window& Text::get(int index){if(index<0 || index>=int(windows_.size()))throw std::runtime_error("Invalid text window slot");return windows_[index];}
void Text::set_text(Window& w,std::string text){w.text=std::move(text);w.cursor=0;w.revealed=w.delay=w.counter=0;}
Bindings Text::bindings(){
    Bindings out;
    out.emplace(640,Binding{0,1,[this](const Invocation& c){state(c).integers[c.operands[0]]=allocate(0);return Reply{};}});
    out.emplace(641,Binding{0,1,[this](const Invocation& c){get(state(c).integers[c.operands[0]]).mode=-1;return Reply{};}});
    out.emplace(642,Binding{0,0,[this](const Invocation&){for(auto& w:windows_){w.mode=-1;w.buffer.clear();}return Reply{};}});
    for(unsigned op:{643u,652u})out.emplace(op,Binding{op==643?3u:4u,0,[this,op](const Invocation& c){auto& w=get(integer(c.arguments[0]));w.x=c.arguments[1];w.y=c.arguments[2];w.z=op==643?-1:c.arguments[3];return Reply{false,0.f};}});
    out.emplace(644,Binding{3,0,[this](const Invocation& c){auto& w=get(integer(c.arguments[0]));w.width=integer(c.arguments[1]);w.height=integer(c.arguments[2]);return Reply{false,0.f};}});
    out.emplace(645,Binding{0,2,[this](const Invocation& c){const float scale=std::bit_cast<float>(c.operands[1]);if(!std::isfinite(scale) || scale<0 || scale>100)throw std::runtime_error("Invalid text scale");get(state(c).integers[c.operands[0]]).scale=scale;return Reply{};}});
    for(unsigned op:{646u,647u})out.emplace(op,Binding{0,5,[this,op](const Invocation& c){auto& w=get(state(c).integers[c.operands[0]]);std::array<unsigned,4> values;for(unsigned i=0;i<4;++i)values[i]=op==646?c.operands[i+1]:unsigned(state(c).integers[c.operands[i+1]]);w.background=color(values[0],values[1],values[2],values[3]);return Reply{};}});
    for(unsigned op:{648u,649u})out.emplace(op,Binding{0,6,[this,op](const Invocation& c){auto& w=get(state(c).integers[c.operands[0]]);if(c.operands[1]>=8)throw std::runtime_error("Invalid text palette index");std::array<unsigned,4> values;for(unsigned i=0;i<4;++i)values[i]=op==648?c.operands[i+2]:unsigned(state(c).integers[c.operands[i+2]]);w.colors[c.operands[1]]=color(values[0],values[1],values[2],values[3]);return Reply{};}});
    out.emplace(650,Binding{0,2,[this](const Invocation& c){get(state(c).integers[c.operands[0]]).lifetime=std::bit_cast<int>(c.operands[1]);return Reply{};}});
    out.emplace(651,Binding{0,2,[this](const Invocation& c){set_text(get(state(c).integers[c.operands[0]]),string(c,c.operands[1]));return Reply{};}});
    for(unsigned op:{653u,654u,655u,656u,657u,658u,659u,660u,661u}){
        const bool automatic=op==654 || op==657 || op==659 || op==661;
        out.emplace(op,Binding{op==655?2u:0u,op==655?0u:automatic?1u:2u,[this,op,automatic](const Invocation& c){
            auto text=string(c,op==655?unsigned(integer(c.arguments[0])):c.operands[0]);
            const int lifetime=op==655?integer(c.arguments[1]):automatic?int(text.size()*4):std::bit_cast<int>(c.operands[1]);
            const bool horizontal=op==658 || op==659;const int index=allocate(horizontal?0:1);
            if(index>=0){auto& w=get(index);w.x=horizontal?132.f:op==656 || op==657?108.f:628.f;w.y=horizontal?280.f:96.f;w.z=-1;w.width=horizontal?380:92;w.height=horizontal?92:276;w.scale=.8f;w.spacing=1;w.line_spacing=8;w.colors[7]={128,128,128,128};w.background={0,0,0,64};set_text(w,std::move(text));w.lifetime=lifetime;}
            return op==655?Reply{false,0.f}:Reply{};
        }});
    }
    for(unsigned op:{662u,663u})out.emplace(op,Binding{0,op==662?3u:2u,[this,op](const Invocation& c){auto& w=get(state(c).integers[c.operands[0]]);const auto source=string(c,c.operands[1]);std::string formatted;unsigned value=op==662?unsigned(state(c).integers[c.operands[2]]):0;
        for(std::size_t p=0;p+1<source.size();p+=2){if(op==662 && static_cast<unsigned char>(source[p])==0x8e && source[p+1]=='I'){formatted+=digits(value);value=0;}
            else if(op==663 && source[p]=='@' && source[p+1]=='d'){if(p+3>=source.size() || source[p+2]<'0' || source[p+2]>'9' || source[p+3]<'0' || source[p+3]>'9')throw std::runtime_error("Invalid text integer format");const unsigned local=(source[p+2]-'0')*10+source[p+3]-'0';formatted+=digits(unsigned(state(c).integers[local]));p+=2;}
            else formatted.append(source,p,2);}
        w.buffer=std::move(formatted);set_text(w,w.buffer);return Reply{};
    }});
    out.emplace(665,Binding{1,0,[this](const Invocation& c){auto& w=get(integer(c.arguments[0]));if(w.text==w.buffer)w.text.clear();w.buffer.clear();return Reply{false,0.f};}});
    out.emplace(666,Binding{2,0,[this](const Invocation& c){auto& w=get(integer(c.arguments[0]));w.buffer+=string(c,unsigned(integer(c.arguments[1])));set_text(w,w.buffer);return Reply{false,0.f};}});
    out.emplace(667,Binding{2,0,[this](const Invocation& c){auto& w=get(integer(c.arguments[0]));if(!std::isfinite(c.arguments[1]))throw std::runtime_error("Invalid text number");std::ostringstream number;number.imbue(std::locale::classic());number<<std::fixed<<std::setprecision(2)<<std::setw(5)<<c.arguments[1];for(unsigned char ch:number.str()){w.buffer+=char(ch=='.'?0x81:0x82);w.buffer+=char(ch=='.'?0x44:ch+0x1f);}set_text(w,w.buffer);return Reply{false,0.f};}});
    // These four handlers explicitly return zero in this executable.
    for(unsigned op:{668u,669u,670u,671u})out.emplace(op,Binding{op==669 || op==670?1u:0u,0,[](const Invocation&){return Reply{false,0.f};}});
    return out;
}
void Text::advance(){
    frame_.clear();
    for(auto& w:windows_){if(w.mode!=0 && w.mode!=1)continue;
        if(--w.counter<0){w.counter=w.delay;
            for(;;){if(w.cursor>=w.text.size()){w.revealed=511;break;}const auto ch=w.text[w.cursor];
                if(ch=='$'){if(w.cursor+4>w.text.size())throw std::runtime_error("Truncated text delay");int delay=0;for(unsigned i=1;i<=3;++i){const char digit=w.text[w.cursor+i];if(digit<'0' || digit>'9')throw std::runtime_error("Invalid text delay");delay=delay*10+digit-'0';}w.counter=w.delay=delay;w.cursor+=4;continue;}
                if(w.cursor+2>w.text.size())throw std::runtime_error("Truncated text character");w.cursor+=2;if(ch=='@')continue;
                ++w.revealed;if(w.delay>0)break;
            }
        }
        frame_.push_back(w);
        if(w.revealed>0 && w.lifetime!=-1 && --w.lifetime<=0)w.mode=-1;
    }
}
void Text::render(AssetImage& image){
    if(frame_.empty())return;if(!font_)font_=std::make_unique<Font>(assets_);
    const float scale=frame_scale(image);
    // The manager submits all backgrounds before all glyphs, in slot order.
    for(const auto& w:frame_){const int x=int(std::nearbyint(w.x))-(w.mode==1?w.width:0),y=int(std::nearbyint(w.y));const unsigned a=std::min(255u,w.background[3]*2);
        for(int py=std::max(0,int((y-8)*scale));py<std::min(int(image.height),int((y+w.height+8)*scale));++py)for(int px=std::max(0,int((x-8)*scale));px<std::min(int(image.width),int((x+w.width+8)*scale));++px){auto* pixel=image.rgba.data()+(std::size_t(py)*image.width+px)*4;for(unsigned c=0;c<3;++c)pixel[c]=static_cast<unsigned char>((w.background[c]*a+pixel[c]*(255-a)+127)/255);}
    }
    for(const auto& w:frame_){const float size=float(std::nearbyint(w.scale*352))/16;const float cell=float(std::nearbyint(w.scale*22));
        const float start_x=w.x-(w.mode==1?cell:0);float x=start_x,y=w.y;unsigned palette=7;int count=0;
        std::array<int,4> clip{int(std::nearbyint(w.mode==1?start_x-w.width:w.x)),int(std::nearbyint(w.y)),int(std::nearbyint(w.mode==1?w.x:w.x+w.width)),int(std::nearbyint(w.y+w.height))};
        for(auto& edge:clip)edge=int(edge*scale);
        const auto newline=[&]{if(w.mode==0){x=start_x;y+=size+w.line_spacing;}else{y=w.y;x-=size+w.line_spacing;}};
        for(std::size_t p=0;p+1<w.text.size() && count<w.revealed-1;){const unsigned first=static_cast<unsigned char>(w.text[p]),second=static_cast<unsigned char>(w.text[p+1]);
            if(first=='$'){p+=4;continue;}p+=2;if(first=='@'){if(second=='n')newline();else palette=second&7;continue;}
            if(!(first&128))continue;
            font_->draw(image,(first<<8)|second,x*scale,y*scale,size*scale,w.colors[palette],clip);
            ++count;if(w.mode==0){x+=size+w.spacing;if(x-start_x>w.width-cell+1)newline();}else{y+=size+w.spacing;if(y-w.y>w.height-cell+1)newline();}
        }
    }
}
}
