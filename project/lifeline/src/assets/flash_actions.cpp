#include "assets/flash.hpp"
#include <charconv>

namespace lifeline {
void FlashPlayer::variable(std::string name,std::string value,std::optional<float> numeric){
    // 15f83c..15f8b0 forwards these assignments even when the stored value is
    // unchanged. A button click and an animation completion are distinct kinds.
    const bool callback=event_sink_->sink && !event_sink_->suppressed && (name=="g_nButtonId" || name=="g_nAnimationEnd");
    if(callback || name=="g_isInputEnable"){
        float number=0;
        if(numeric)number=*numeric;
        else {
            // 1600a0 is the disc's decimal accumulator, not strtof. Its
            // fractional loop includes the dot itself; preserve that behavior.
            std::size_t p=0;while(p<value.size() && value[p]!='.')number=number*10+float(std::int8_t(value[p++])-'0');
            float scale=.1f;while(p<value.size()){number+=float(std::int8_t(value[p++])-'0')*scale;scale*=.1f;}
        }
        if(!std::isfinite(number) || double(number)<INT32_MIN || double(number)>INT32_MAX)throw std::runtime_error("Invalid CF callback value");
        const auto integer=int(std::trunc(number));
        if(callback)event_sink_->sink(name=="g_nButtonId"?1u:3u,integer);
        else event_sink_->input_enabled=integer!=0;
    }
    variables_[std::move(name)]=std::move(value);
}
// Native execution of the CF button's exported ActionScript expressions.
// These are UI timelines, unrelated to the game's machine-code architecture.
void FlashPlayer::execute_bytecode(std::span<const unsigned char> bytes){
    using Value=std::variant<std::string,double>;std::vector<Value> stack;
    const auto number=[](const Value& v){if(const auto* n=std::get_if<double>(&v))return *n;const auto& s=std::get<std::string>(v);if(s.empty())return 0.;double n=0;auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);if(error!=std::errc{} || end!=s.data()+s.size())throw std::runtime_error("CF numeric expression is not a number");return n;};
    const auto string=[](const Value& v){if(const auto* s=std::get_if<std::string>(&v))return *s;return std::to_string(std::get<double>(v));};
    const auto pop=[&](){if(stack.empty())throw std::runtime_error("CF expression stack underflow");auto v=std::move(stack.back());stack.pop_back();return v;};
    std::size_t p=0;
    for(unsigned budget=0;budget<4096;++budget){
        if(p>=bytes.size())throw std::runtime_error("CF expression has no terminator");
        const auto opcode=bytes[p++];if(!opcode)return;std::size_t length=0;
        if(opcode>=128){if(bytes.size()-p<2)throw std::runtime_error("Truncated CF action");length=le16(bytes.data()+p);p+=2;}
        if(length>bytes.size()-p)throw std::runtime_error("CF action payload outside bounds");const auto end=p+length;
        if(opcode==0x96){
            while(p<end){if(bytes[p++]!=0)throw std::runtime_error("Unsupported CF expression value type");const auto begin=p;while(p<end && bytes[p])++p;if(p==end)throw std::runtime_error("Unterminated CF expression string");stack.emplace_back(std::string(bytes.begin()+begin,bytes.begin()+p));++p;}
        }else if(opcode==0x1c){const auto name=string(pop());const auto found=variables_.find(name);stack.emplace_back(found==variables_.end()?std::string{}:found->second);}
        else if(opcode==0x1d){auto value=pop();auto name=string(pop());const auto* n=std::get_if<double>(&value);variable(std::move(name),string(value),n?std::optional<float>(float(*n)):std::nullopt);}
        else if(opcode==0x22){const auto property=number(pop());const auto target=string(pop());if(!target.empty() || property!=4)throw std::runtime_error("Unrecovered CF timeline property");stack.emplace_back(double(frame_+1));}
        else if(opcode==0x0b || opcode==0x0e || opcode==0x0f){const auto b=number(pop()),a=number(pop());stack.emplace_back(opcode==0x0b?a-b:opcode==0x0e?double(a==b):double(a<b));}
        else if(opcode==0x12)stack.emplace_back(double(number(pop())==0));
        else if(opcode==0x06 || opcode==0x07){playing_=opcode==0x06;if(jump_>=0)jump_playing_=playing_?1:0;}
        else if(opcode==0x99 || opcode==0x9d){
            if(length!=2)throw std::runtime_error("Invalid CF branch");const bool take=opcode==0x99 || number(pop())!=0;
            if(take){const auto offset=std::int16_t(le16(bytes.data()+p));const auto target=std::int64_t(end)+offset;if(target<0 || target>=std::int64_t(bytes.size()))throw std::runtime_error("CF branch outside action");p=std::size_t(target);continue;}
        }else if(opcode==0x9f){
            if(length!=1 || bytes[p]>1)throw std::runtime_error("Unsupported CF dynamic frame flags");const auto target=number(pop())-1;
            if(!std::isfinite(target) || target<0 || target>=frames_->size())throw std::runtime_error("CF dynamic frame outside timeline");jump_=int(target);playing_=bytes[p]!=0;jump_playing_=playing_?1:0;
        }else throw std::runtime_error("Unrecovered CF expression opcode "+std::to_string(opcode));
        if(stack.size()>128)throw std::runtime_error("CF expression stack limit");p=end;
    }throw std::runtime_error("CF expression instruction budget exhausted");
}
}
