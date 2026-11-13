#include "script/speech_input.hpp"
#include <stdexcept>
#include <cmath>
#include <bit>

namespace lifeline::script {
Bindings SpeechInput::bindings(){
    Bindings result;
    result.emplace(161,Binding{0,0,[this](const Invocation&){return Reply{false,float(device_state_)};}});
    result.emplace(630,Binding{1,0,[this](const Invocation& c){
        const float value=c.arguments[0];if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>=2147483648.)throw std::runtime_error("Invalid unavailable-speech feedback flag");
        unavailable_feedback_enabled_=std::int32_t(value)!=0;return Reply{false,0.f};
    }});
    result.emplace(631,Binding{0,0,[this](const Invocation&){return Reply{false,unavailable_feedback_enabled_?1.f:0.f};}});
    for(const auto opcode:{597u,598u})result.emplace(opcode,Binding{0,1,[this,opcode](const Invocation& c){
        if(!c.state)throw std::logic_error("Speech result binding requires worker locals");
        // 143090 normalizes -3 only for operation 597; reading does not consume.
        if(opcode==597 && result_==-3)result_=-1;
        c.state->integers[c.operands[0]]=result_;
        if(result_!=-1)result_read_=true;
        return Reply{};
    }});
    result.emplace(599,Binding{0,0,[this](const Invocation&){enabled_=true;result_=-1;result_read_=false;++gate_changes_;return Reply{};}});
    result.emplace(600,Binding{0,0,[this](const Invocation&){enabled_=false;result_=-1;result_read_=false;--gate_changes_;return Reply{};}});
    for(unsigned op:{607u,608u})result.emplace(op,Binding{0,1,[this,op](const Invocation& c){
        if(!c.state)throw std::logic_error("Speech component query requires worker locals");
        c.state->integers[c.operands[0]]=parsed_.categories[op-606];return Reply{};
    }});
    result.emplace(626,Binding{1,0,[this](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value) || std::nearbyint(value)<0 || std::nearbyint(value)>=8)throw std::runtime_error("Speech sequence query outside original result");
        return Reply{false,float(parsed_.sequence[unsigned(std::nearbyint(value))])};
    }});
    result.emplace(610,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::logic_error("Speech score query requires worker locals");
        c.state->integers[c.operands[0]]=score_;return Reply{};
    }});
    for(const auto opcode:{611u,612u})result.emplace(opcode,Binding{0,2,[this,opcode](const Invocation& c){
        if(!c.state)throw std::logic_error("Speech history query requires worker locals");
        const auto bin=c.state->integers[c.operands[1]];
        if(bin<0 || bin>=10)throw std::runtime_error("Speech history range outside 0..9");
        const auto count=score_counts_[bin];
        const auto value=opcode==611?count:count?score_sums_[bin]/count:0;
        c.state->integers[c.operands[0]]=std::bit_cast<std::int32_t>(std::uint32_t(value));return Reply{};
    }});
    result.emplace(613,Binding{0,1,[this](const Invocation& c){
        if(!c.state)throw std::logic_error("Speech average requires worker locals");
        std::uint32_t count=0;std::uint64_t sum=0;
        for(unsigned i=0;i<10;++i){count+=std::uint32_t(score_counts_[i]);sum+=score_sums_[i];}
        // The original helper traps on division by zero (2ad9f8); do not
        // fabricate a score when a script requests an empty total history.
        if(!count)throw std::runtime_error("Speech average requested before any scored word result");
        c.state->integers[c.operands[0]]=std::bit_cast<std::int32_t>(std::uint32_t(std::bit_cast<std::int64_t>(sum)/std::bit_cast<std::int32_t>(count)));return Reply{};
    }});
    result.emplace(614,Binding{0,0,[this](const Invocation&){clear_history();return Reply{};}});
    result.emplace(632,Binding{0,0,[this](const Invocation&){return Reply{false,float(gate_changes_)};}});
    result.emplace(633,Binding{1,0,[this](const Invocation& c){
        const auto value=c.arguments[0];if(!std::isfinite(value)||double(value)<-2147483648.0||double(value)>=2147483648.0)throw std::runtime_error("Invalid speech gate counter");
        gate_changes_=std::int32_t(value);return Reply{false,float(gate_changes_)};
    }});
    return result;
}
}
