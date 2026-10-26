#include "script/runtime.hpp"
#include "script/battle_targeting.hpp"
#include "script/save_menu.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace lifeline::script {
bool Runtime::title_back(){
    // PC Back extension: re-enter the original Start wait in the same call
    // frame. Do not restart startup, music, resources, or the title scene.
    if(program_->name()!="entry_1004.cb"||pc_<0x173c/4||pc_>0x2154/4)return false;
    state_.integers[12]=0;
    expression_.clear();expression_top_=expression_base_=0;
    arguments_.clear();chained_arguments_.clear();
    pc_=0x0e0c/4;result_=Result::running;
    return true;
}
namespace {
std::int32_t integer(float f){
    if(!std::isfinite(f) || double(f)<std::numeric_limits<std::int32_t>::min() || double(f)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Script integer conversion overflow");
    return std::int32_t(f);
}
bool compare(unsigned mode,float a,float b){
    switch(mode){case 0:return a==b;case 1:return a!=b;case 2:return a>b;case 3:return a>=b;case 4:return a<b;case 5:return a<=b;default:throw std::runtime_error("Unknown script comparison");}
}
float divide(float numerator,float denominator){
    // R5900 DIV.S does not raise a C++-style divide-by-zero exception. With
    // FCR31's authored default mask it writes the EE saturated result and the
    // script continues. Preserve the original bit result, including flushed
    // exponent-zero inputs and its round-to-nearest quotient.
    const auto nbits=std::bit_cast<std::uint32_t>(numerator),dbits=std::bit_cast<std::uint32_t>(denominator);
    const auto sign=(nbits^dbits)&0x80000000u;
    const int ne=(nbits>>23)&255,de=(dbits>>23)&255;
    if(!de)return std::bit_cast<float>(sign|0x7fffffffu);
    if(!ne)return std::bit_cast<float>(sign);
    std::uint64_t n=(nbits&0x7fffffu)|0x800000u;
    const std::uint64_t d=(dbits&0x7fffffu)|0x800000u;
    int exponent=ne-de+127;if(n<d){n<<=1;--exponent;}n<<=23;
    auto quotient=n/d;const auto remainder=n%d;
    if(remainder*2>d || (remainder*2==d && (quotient&1)))++quotient;
    if(quotient==0x1000000u){quotient>>=1;++exponent;}
    if(exponent<=0)return std::bit_cast<float>(sign);
    if(exponent>255)return std::bit_cast<float>(sign|0x7fffffffu);
    return std::bit_cast<float>(sign|(std::uint32_t(exponent)<<23)|(std::uint32_t(quotient)&0x7fffffu));
}
}
Runtime::Runtime(std::shared_ptr<const Program> p,State& state,const Bindings& bindings,std::uint32_t entry):program_(std::move(p)),state_(state),bindings_(bindings),pc_(entry){
    if(!program_)throw std::invalid_argument("Missing event program");jump(entry);
}
void Runtime::fail(const std::string& why) const {
    std::ostringstream s;s<<program_->name()<<" at byte 0x"<<std::hex<<pc_*4<<" (opcode "<<std::dec<<program_->word(pc_)<<"): "<<why;throw std::runtime_error(s.str());
}
Value Runtime::pop(){
    const auto cell=std::int64_t(state_.expression_worker)*128+expression_top_-2;
    if(cell<0||cell+1>=state_.expression_cells->size())fail("expression stack underflow outside retained storage");
    expression_top_-=2;
    if(expression_.empty())return {(*state_.expression_cells)[std::size_t(cell)]};
    auto v=expression_.back();expression_.pop_back();v.number=(*state_.expression_cells)[std::size_t(cell)];return v;
}
void Runtime::push(Value v){
    const auto cell=std::int64_t(state_.expression_worker)*128+expression_top_;
    if(cell<0||cell+1>=state_.expression_cells->size())fail("expression stack limit");
    (*state_.expression_cells)[std::size_t(cell)]=v.number;
    // 103c00 uses tag 2 for native scalar results; explicit literal/variable
    // handlers use type in the upper half and local index in the lower half.
    const auto tag=v.type==2&&program_->word(pc_)!=889?2u:(v.type<<16)|((v.type==1||v.type==3)?v.index:0u);
    (*state_.expression_cells)[std::size_t(cell)+1]=float(tag);
    expression_top_+=2;expression_.push_back(v);
}
void Runtime::jump(std::uint32_t target){if(target>=program_->size())fail("branch target outside script");pc_=target;}
float Runtime::operand(unsigned type,std::uint32_t value) const {
    switch(type){
    case 0:return float(std::bit_cast<std::int32_t>(value));
    case 1:{const auto it=state_.integers.find(value);return it==state_.integers.end()?0:float(it->second);}
    case 2:return std::bit_cast<float>(value);
    case 3:{const auto it=state_.floats.find(value);return it==state_.floats.end()?0:it->second;}
    default:fail("unsupported operand type");
    }
}
void Runtime::release_arrays(std::vector<std::uint32_t>& arrays){
    for(const auto base:arrays){const auto found=state_.arrays->find(base);if(found==state_.arrays->end())continue;
        const auto end=base+found->second;state_.data->erase(state_.data->lower_bound(base),state_.data->lower_bound(end));state_.arrays->erase(found);}
    arrays.clear();
}
Runtime::~Runtime(){release_arrays(arrays_);for(auto& frame:frames_)release_arrays(frame.arrays);}
void Runtime::restore(){for(auto i=saved_.rbegin();i!=saved_.rend();++i){if(i->type==1)state_.integers[i->index]=i->integer;else state_.floats[i->index]=i->value;}saved_.clear();release_arrays(arrays_);}
Result Runtime::step(){
    const auto op=program_->word(pc_);const auto w=[&](unsigned n){return program_->word(pc_+n);};
    const auto native=bindings_.find(op);
    const bool chain_expression=op==887 || op==888 || op==889 || op==890 || op==891 || op==893 || op==900 || op==901;
    const bool chain_binding=native!=bindings_.end() && native->second.chains_arguments;
    if(!chain_expression && !chain_binding && op!=2 && op!=18)chained_arguments_.clear();
    switch(op){
    case 0:case 3:++pc_;break;
    case 1:jump(w(1));break;
    case 2:case 18:{
        if(op==2 && w(2)!=0 && w(2)!=2)fail("aggregate return signature not recovered");
        if(frames_.size()>=128)fail("call depth limit");
        auto target=w(1);
        if(op==18){
            // 10e788: an integer local indexes a live table of word addresses.
            const auto address=std::int64_t(w(1))+state_.integers[w(2)];
            if(address<0 || address>UINT32_MAX || !state_.data_contains(*program_,unsigned(address)))fail("indirect call table outside live data");
            const auto found=state_.data->find(unsigned(address));
            target=found==state_.data->end()?program_->word(unsigned(address)):std::bit_cast<unsigned>(found->second);
        }
        if(target>=program_->size())fail("call target outside script");
        // Authored helper entries may begin with one or more inline diagnostic
        // messages before their parameter declaration (entry_001 uses this for
        // both lift interpolation helpers).  The parameters still belong to
        // this call and remain pending while the diagnostic executes.
        auto declaration=target;
        while(declaration+2<program_->size() && program_->word(declaration)==676)declaration+=2;
        const unsigned count=program_->word(declaration)==33?program_->word(declaration+1):0;
        const auto explicit_count=std::min<std::size_t>(count,arguments_.size());
        const auto missing=count-explicit_count;
        if(missing>chained_arguments_.size())fail("script call argument underflow");
        // ARG is also used while evaluating arguments of an outer call. Only
        // this callee's declared tail belongs to it (10d7f4..10d800).
        std::vector<float> parameters;
        parameters.reserve(count);
        parameters.insert(parameters.end(),chained_arguments_.end()-missing,chained_arguments_.end());
        parameters.insert(parameters.end(),arguments_.end()-explicit_count,arguments_.end());
        arguments_.resize(arguments_.size()-explicit_count);chained_arguments_.clear();
        if(const auto helper=program_->battle_helpers().find(target);helper!=program_->battle_helpers().end()){
            push({battle_target_helper({helper->second.first,helper->second.second},*program_,state_,parameters)});
            pc_+=3;break;
        }
        frames_.push_back({pc_+3,std::move(expression_),std::move(saved_),std::move(arguments_),std::move(parameters_),std::move(arrays_),expression_top_,expression_base_});
        expression_base_=++expression_top_; // 10d618 reserves one float cell between call frames.
        expression_.clear();saved_.clear();arguments_.clear();arrays_.clear();parameters_=std::move(parameters);jump(target);break;
    }
    case 4:restore();return Result::complete;
    case 5:
        if(frames_.empty())fail("return without caller");
        {const auto value=pop().number;restore();auto frame=std::move(frames_.back());frames_.pop_back();expression_=std::move(frame.expression);expression_top_=frame.top;expression_base_=frame.base;saved_=std::move(frame.saved);arguments_=std::move(frame.arguments);parameters_=std::move(frame.parameters);arrays_=std::move(frame.arrays);push({value});jump(frame.resume);}break;
    case 6:{const auto types=w(1);const auto a=operand((types>>16)&255,w(2)),b=operand((types>>8)&255,w(3));jump(compare(types&255,a,b)?pc_+5:w(4));break;}
    case 7:case 8:{
        const auto mask=w(1),count=mask>>27;bool matched=op==7;
        for(unsigned i=0;i<count;++i){const bool equal=state_.flags[w(3+i)]==bool((mask>>i)&1);if(op==7)matched&=equal;else matched|=equal;}
        jump(matched?pc_+3+count:w(2));break;
    }
    case 11:{auto& count=state_.integers[w(1)];if(count==-1)pc_+=3;else if(count==0)jump(w(2));else {--count;pc_+=3;}break;}
    case 12:{auto& count=state_.integers[w(1)];if(count==0){pc_+=2;break;}if(count!=-1)--count;return Result::waiting;}
    case 25:++pc_;return Result::yielded;
    case 32:{
        const auto descriptor=w(1),type=descriptor>>16,index=descriptor&65535;
        if(type!=1 && type!=3)fail("unsupported scoped variable type");
        saved_.push_back({type,index,state_.floats[index],state_.integers[index]});pc_+=2;break;
    }
    case 33:{
        const unsigned count=w(1);if(count!=parameters_.size())fail("parameter declaration mismatch");
        // Integer and float parameters each occupy their bank from slot zero;
        // the original handler counts each kind, not the descriptor's low half.
        for(unsigned i=0;i<count;++i)if((w(2+i)>>16)!=1 && (w(2+i)>>16)!=3)fail("aggregate parameter type not recovered");
        unsigned ints=0,floats=0;
        for(unsigned i=0;i<count;++i){const auto type=w(2+i)>>16,index=type==1?ints++:floats++;
            saved_.push_back({type,index,state_.floats[index],state_.integers[index]});
            if(type==1)state_.integers[index]=integer(parameters_[i]);else state_.floats[index]=parameters_[i];}
        parameters_.clear();pc_+=2+count;break;
    }
    case 34:{const auto condition=pop().number;jump(condition==0?w(1):pc_+2);break;}
    case 35:state_.integers[w(1)]=std::bit_cast<std::int32_t>(w(2));pc_+=3;break;
    case 36:state_.floats[w(1)]=std::bit_cast<float>(w(2));pc_+=3;break;
    case 42:state_.floats[w(1)]=float(state_.integers[w(2)]);pc_+=3;break;
    case 43:state_.integers[w(1)]=std::bit_cast<std::int32_t>(std::uint32_t(state_.integers[w(1)])+w(2));pc_+=3;break;
    case 44:state_.floats[w(1)]+=std::bit_cast<float>(w(2));pc_+=3;break;
    case 46:state_.floats[w(1)]+=state_.floats[w(2)];pc_+=3;break;
    case 52:state_.floats[w(1)]*=std::bit_cast<float>(w(2));pc_+=3;break;
    case 54:state_.floats[w(1)]*=state_.floats[w(2)];pc_+=3;break;
    case 59:case 61:{
        auto& dividend=state_.integers[w(1)];
        const auto divisor=op==59?std::bit_cast<std::int32_t>(w(2)):state_.integers[w(2)];
        if(!divisor)fail("integer division by zero");
        // EE signed DIV retains INT_MIN for the overflow pair.
        if(dividend!=std::numeric_limits<std::int32_t>::min()||divisor!=-1)dividend/=divisor;
        pc_+=3;break;
    }
    case 60:state_.floats[w(1)]=divide(state_.floats[w(1)],std::bit_cast<float>(w(2)));pc_+=3;break;
    case 71:{
        // 111970: signed integer remainder of two ARG values, returned as float.
        if(arguments_.size()<2)fail("remainder argument count");
        const auto count=arguments_.size();const auto divisor=integer(arguments_[count-1]),dividend=integer(arguments_[count-2]);
        if(!divisor)fail("remainder by zero");
        const auto value=dividend==std::numeric_limits<std::int32_t>::min() && divisor==-1?0:dividend%divisor;
        arguments_.resize(count-2);push({float(value)});++pc_;break;
    }
    case 73:{
        // 111b68 -> 1d51a8: bearing between points in the plane normal to axis.
        if(arguments_.size()<7)fail("bearing argument underflow");
        const auto begin=arguments_.size()-7;const int axis=integer(arguments_[begin]);
        if(axis<0 || axis>2)fail("bearing axis outside 0..2");
        const unsigned x=(axis+1)%3,y=(axis+2)%3;
        const float value=std::atan2(arguments_[begin+4+y]-arguments_[begin+1+y],arguments_[begin+4+x]-arguments_[begin+1+x]);
        arguments_.resize(begin);push({value});++pc_;break;
    }
    case 75:case 76:{
        if(arguments_.empty())fail("trigonometric argument underflow");
        const float value=arguments_.back();arguments_.pop_back();
        // 1117e0/111840 call the original single-precision sin/cos services.
        push({op==75?std::sin(value):std::cos(value)});++pc_;break;
    }
    case 109:case 110:{if(arguments_.empty())fail("flag argument count");const auto flag=integer(arguments_.back());if(flag<0 || flag>=32768)fail("flag index outside original bank");state_.flags[flag]=op==109;arguments_.pop_back();push({op==109?1.f:0.f});++pc_;break;}
    case 111:state_.flags.clear();++pc_;break;
    case 162:{
        if(arguments_.empty() || !std::isfinite(arguments_.back()))fail("invalid millisecond wait");
        arguments_.back()-=std::bit_cast<float>(0x41855557u);
        if(arguments_.back()>0)return Result::waiting;
        arguments_.pop_back();push({0.f});++pc_;break;
    }
    case 1085:case 1086:{
        const unsigned count=op==1085?2u:3u;
        if(arguments_.size()<count)fail("script data accessor argument count");
        const auto begin=arguments_.size()-count;
        // 103c68 resolves a byte offset relative to CB; both accessors take
        // a word base, multiply by four, then add the integer element index.
        const float offset=arguments_[begin]*4;
        const auto base=integer(offset+std::copysign(.5f,offset));
        const auto index=integer(arguments_[begin+1]);
        const auto address=std::int64_t(base)+std::int64_t(index)*4;
        if(address<0 || address%4 || std::uint64_t(address)/4>UINT32_MAX || !state_.data_contains(*program_,std::uint32_t(address/4)))fail("script data accessor outside live data");
        const auto word=std::uint32_t(address/4);
        if(op==1086){(*state_.data)[word]=arguments_[begin+2];push({0.f});}
        else {const auto found=state_.data->find(word);push({found==state_.data->end()?std::bit_cast<float>(program_->word(word)):found->second});}
        arguments_.resize(begin);++pc_;break;
    }
    case 887:expression_.clear();expression_top_=expression_base_;++pc_;break;
    case 888:push({float(std::bit_cast<std::int32_t>(w(1))),0});pc_+=2;break;
    case 889:push({std::bit_cast<float>(w(1)),2});pc_+=2;break;
    case 890:push({float(state_.integers[w(1)]),1,w(1)});pc_+=2;break;
    case 891:push({state_.floats[w(1)],3,w(1)});pc_+=2;break;
    case 893:push({float(w(1)),5});pc_+=2;break;
    case 895:{const auto value=pop(),target=pop();if(target.type==1)state_.integers[target.index]=integer(value.number+std::copysign(.01f,value.number));else if(target.type==3)state_.floats[target.index]=value.number;else if(target.type==4){if(!state_.data_contains(*program_,target.index))fail("assignment through expired array");(*state_.data)[target.index]=value.number;}else fail("assignment requires variable");push({value.number});++pc_;break;}
    case 896:case 897:case 898:case 899:{const auto b=pop().number,a=pop().number;push({op==896?a+b:op==897?a-b:op==898?a*b:divide(a,b)});++pc_;break;}
    case 900:{const auto v=pop();push({-v.number});++pc_;break;}
    case 901:if(arguments_.size()>=128)fail("argument stack limit");arguments_.push_back(pop().number);++pc_;break;
    case 902:case 903:{const auto b=integer(pop().number),a=integer(pop().number);push({float(op==902?a&b:a|b)});++pc_;break;}
    case 904:case 905:case 906:case 907:case 908:case 909:{
        const auto b=pop().number,a=pop().number;
        constexpr unsigned modes[]={0,1,2,4,3,5};push({compare(modes[op-904],a,b)?1.f:0.f});++pc_;break;
    }
    case 912:{const auto flag=integer(pop().number);if(flag<0 || flag>=32768)fail("flag index outside original bank");push({state_.flags[flag]?1.f:0.f});++pc_;break;}
    case 913:{
        if(arguments_.empty())fail("script array argument underflow");const auto address=arguments_.back();arguments_.pop_back();
        if(!std::isfinite(address) || address<0 || double(address)>UINT32_MAX || std::floor(address)!=address || !state_.data_contains(*program_,std::uint32_t(address)))fail("array base outside live script data");
        push({address*4,4,std::uint32_t(address)});++pc_;break;
    }
    case 910:case 911:case 915:{
        if(arguments_.size()<2)fail("script array index underflow");const auto index=arguments_.back();arguments_.pop_back();const auto base=arguments_.back();arguments_.pop_back();
        const double address=double(base)+double(index)*4;
        if(!std::isfinite(address) || address<0 || address/4>UINT32_MAX || std::fmod(address,4)!=0 || !state_.data_contains(*program_,std::uint32_t(address/4)))fail("array element outside live script data");
        const auto word=std::uint32_t(address/4);
        if(op==911)push({float(address),4,word});
        else {const auto found=state_.data->find(word);const float value=found==state_.data->end()?std::bit_cast<float>(program_->word(word)):found->second;
            // 1270f0 reads the same array address as 910, then CVT.S.W
            // interprets the stored bits as a signed integer.
            push({op==915?float(std::bit_cast<std::int32_t>(value)):value});}
        ++pc_;break;
    }
    case 914:{
        if(arguments_.empty())fail("temporary array size missing");
        const auto size=integer(arguments_.back());
        const int limit=has_native_save_menu(program_->name())?128*save_slot_count:1024;
        if(size<0 || size>limit)fail("temporary array exceeds native script stack limit");
        const auto count=unsigned(size)+1; // 127714 reserves one additional cell.
        std::uint64_t base=program_->size();
        for(const auto& [start,length]:*state_.arrays){if(base+count<=start)break;base=std::max(base,std::uint64_t(start)+length);}
        // Addresses travel through the original float expression ABI. Keep
        // every byte offset exactly representable; never expose host pointers.
        if(base+count>0x400000)fail("temporary array address exceeds exact float range");
        const auto word=std::uint32_t(base);state_.arrays->emplace(word,count);arrays_.push_back(word);
        for(unsigned i=0;i<count;++i)(*state_.data)[word+i]=0;
        arguments_.pop_back();push({float(word*4),4,word});++pc_;break;
    }
    default:{
        const auto binding=native;if(binding==bindings_.end())fail("native binding not implemented");
        const auto& b=binding->second;if(arguments_.size()<b.arguments)fail("native binding argument count mismatch");
        const auto prefix=arguments_.size()-b.arguments;
        std::vector<std::uint32_t> operands;for(unsigned i=0;i<b.operands;++i)operands.push_back(w(i+1));
        Reply reply;
        try{reply=b.invoke({*program_,pc_,op,std::span<const float>(arguments_).subspan(prefix),operands,&state_});}
        catch(const std::exception& e){fail(e.what());}
        if(reply.waiting)return Result::waiting;
        if(b.chains_arguments)chained_arguments_.insert(chained_arguments_.end(),arguments_.begin()+prefix,arguments_.end());
        arguments_.resize(prefix);if(reply.value)push({*reply.value});pc_+=1+b.operands;break;
    }
    }
    return Result::running;
}
Result Runtime::advance(unsigned budget){
    if(interrupted_)return result_=Result::complete;
    if(result_==Result::complete)return result_;
    for(unsigned i=0;i<budget;++i){result_=step();if(interrupted_)return result_=Result::complete;if(result_!=Result::running)return result_;}
    fail("instruction budget exhausted without yielding");
}
void Runtime::run_until(std::uint32_t boundary,unsigned budget){
    for(unsigned i=0;i<budget;++i){if(pc_==boundary)return;if(step()!=Result::running)fail("bootstrap block yielded before its boundary");}
    fail("bootstrap block instruction budget exhausted");
}
}

