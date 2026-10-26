#include "script/process.hpp"
#include "script/vectors.hpp"
#include "script/memory.hpp"
#include "core/state_archive.hpp"
#include <stdexcept>
#include <cmath>
#include <limits>
#include <bit>

namespace lifeline::script {
namespace {
unsigned worker_slot(float value){
    if(!std::isfinite(value) || value<0 || value>=24)throw std::runtime_error("Invalid script worker slot");
    return unsigned(value);
}
}
Process::Process(std::shared_ptr<const Program> program,EventTable events,Bindings bindings,Entry entry,std::function<void(std::string_view)> trace)
    :program_(std::move(program)),events_(std::move(events)),bindings_(std::move(bindings)),trace_(std::move(trace)){
    groups_.emplace(1,ScriptGroup{program_});group_names_.emplace(program_->name(),1);
    const auto bind=[&](unsigned opcode,Binding binding){if(!bindings_.emplace(opcode,std::move(binding)).second)throw std::invalid_argument("Cannot override script dispatcher operation");};
    // These individual original handlers only advance PC. This is not a
    // fallback for unknown opcodes; operand counts were recovered per handler.
    constexpr std::pair<unsigned,unsigned> inactive_operations[]{
        {116,1},{130,1},{131,2},{295,7},{564,4},{565,4},{680,0},{681,0},{682,0},
        {761,0},{762,0},{763,1},{764,1},{766,3},{767,2},{768,1},{769,1},
        {770,1},{771,0},{772,1},{773,0},{774,0},{775,0},{776,0},{777,0},{778,0},
        {779,8},{780,1},{781,0},{782,0},{783,0},{784,0},{785,1},{786,0},{787,1},
        {788,2},{789,5},{790,1}
    };
    for(const auto [opcode,operands]:inactive_operations)bind(opcode,{0,operands,[](const Invocation&){return Reply{};}});
    // Original heap coalescing/dump requests (13d590/13d5d8) have no script
    // result. Native allocation and RAII replace that console heap service.
    for(unsigned opcode:{114u,115u})bind(opcode,{0,0,[this,opcode](const Invocation&){
        if(trace_)trace_(opcode==114?"Heap maintenance: native allocator manages free blocks":"Heap diagnostic: no console heap in native runtime");return Reply{};
    }});
    for(auto& [opcode,binding]:curves_.bindings())bind(opcode,std::move(binding));
    for(auto& [opcode,binding]:vector_bindings())bind(opcode,std::move(binding));
    for(auto& [opcode,binding]:memory_bindings())bind(opcode,std::move(binding));
    // 12e788 writes bool(inline operand) to the shared process, not a worker.
    bind(365,{0,1,[this](const Invocation& c){scene_script_option_=c.operands[0]!=0;return Reply{};}});
    // 13feb8/13fef8/13ff48 share a process timer. 1003c4 increments it
    // by one after the cooperative workers, not once per waiting worker.
    bind(1127,{0,0,[this](const Invocation&){command_timer_=0;return Reply{false,0.f};}});
    bind(1128,{0,0,[this](const Invocation&){command_timer_=-1;return Reply{false,0.f};}});
    bind(1129,{1,0,[this](const Invocation& c){
        if(!std::isfinite(c.arguments[0]))throw std::runtime_error("Invalid command timer threshold");
        if(c.arguments[0]>command_timer_)return Reply{true,{}};
        return Reply{false,0.f};
    }});
    bind(95,{0,1,[this](const Invocation& call){spawn(call.operands[0]);return Reply{};}});
    bind(136,{1,0,[this](const Invocation& c){
        const auto address=std::nearbyint(c.arguments[0]);
        if(!std::isfinite(address) || address<0 || double(address)>=c.program.size())throw std::runtime_error("Invalid script filename address");
        return Reply{false,float(load_script(c.program.string(std::uint32_t(address))))};
    }});
    bind(137,{0,2,[this](const Invocation& c){const auto handle=load_script(c.program.string(c.operands[0]));current_state().integers[c.operands[1]]=handle;return Reply{};}});
    bind(138,{0,1,[this](const Invocation& c){finish_group(std::bit_cast<std::int32_t>(c.operands[0]));return Reply{};}});
    bind(139,{0,1,[this](const Invocation& c){finish_group(current_state().integers[c.operands[0]]);return Reply{};}});
    bind(140,{0,1,[this](const Invocation& c){const auto found=groups_.find(current_state().integers[c.operands[0]]);if(found!=groups_.end())found->second.registered=false;return Reply{};}});
    bind(141,{0,2,[this](const Invocation& c){auto& state=current_state();const auto found=groups_.find(state.integers[c.operands[0]]);if(found!=groups_.end() && found->second.registered)state.integers[c.operands[1]]=found->second.result;return Reply{};}});
    bind(142,{0,1,[this](const Invocation& c){
        const auto handle=current_state().integers[c.operands[0]];const auto found=groups_.find(handle);
        if(found!=groups_.end() && found->second.registered)for(const auto& worker:workers_)if(worker.running && worker.group==handle)return Reply{true,{}};
        return Reply{};
    }});
    bind(143,{0,1,[this](const Invocation& c){
        if(!file_loader_)throw std::runtime_error("Script event table loader is not connected");
        events_=EventTable(c.program,file_loader_(c.program.string(c.operands[0])));return Reply{};
    }});
    // 13d918 / 13db18 / 13dc98 allocate only ordinary, unreserved slots.
    // The destination local is written before reading the three parameters;
    // scripts may deliberately alias that destination with an input local.
    for(const auto opcode:{132u,133u,134u})bind(opcode,{0,opcode==132?2u:5u,[this,opcode](const Invocation& c){
        const auto slot=free_slot();auto& caller=current_state();
        if(slot<0){caller.integers[c.operands[1]]=-1;return Reply{};}
        replace(unsigned(slot),c.operands[0]);
        caller.integers[c.operands[1]]=slot;
        if(opcode!=132)for(unsigned i=0;i<3;++i)
            workers_[slot].state.integers[i]=opcode==133?caller.integers[c.operands[i+2]]:std::bit_cast<std::int32_t>(c.operands[i+2]);
        return Reply{};
    }});
    bind(135,{2,0,[this](const Invocation& c){
        const auto address=c.arguments[0],parameter=c.arguments[1];
        if(!std::isfinite(address) || address<0 || double(address)>=c.program.size() || !std::isfinite(parameter) || double(parameter)<std::numeric_limits<std::int32_t>::min() || double(parameter)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Invalid worker entry/parameter");
        if(free_slot()<0)return Reply{false,-1.f};const auto slot=spawn(std::uint32_t(address));workers_[slot].state.integers[0]=std::int32_t(parameter);return Reply{false,float(slot)};
    }});
    bind(149,{0,0,[this](const Invocation&){if(!current_)throw std::logic_error("No current script worker");return Reply{false,float(*current_)};}});
    bind(150,{0,1,[this](const Invocation& c){
        if(!c.state)throw std::logic_error("Signal allocation requires worker state");
        auto& destination=c.state->integers[c.operands[0]];destination=-1;
        for(unsigned i=0;i<signals_.size();++i)if(!signals_[i].allocated){signals_[i]={true,0,0};destination=int(i);break;}
        return Reply{};
    }});
    for(unsigned opcode=151;opcode<=157;++opcode)bind(opcode,{0,opcode<=153?1u:2u,[this,opcode](const Invocation& c){
        if(!c.state)throw std::logic_error("Signal operation requires worker state");
        const auto id=std::uint32_t(c.state->integers[c.operands[0]]);
        if(id>=signals_.size())return Reply{};
        auto& signal=signals_[id];
        switch(opcode){
        case 151:signal.allocated=false;break;
        case 152:++signal.references;break;
        case 153:if(signal.references)--signal.references;break;
        case 154:c.state->integers[c.operands[1]]=signal.references!=0;break;
        case 155:return Reply{(signal.references!=0)!=(c.operands[1]!=0),{}};
        case 156:signal.value=std::bit_cast<std::int32_t>(c.operands[1]);break;
        case 157:c.state->integers[c.operands[1]]=signal.value;break;
        }
        return Reply{};
    }});
    bind(144,{0,3,[this](const Invocation& c){
        auto& state=current_state();post(*current_,{state.integers[c.operands[0]],state.integers[c.operands[1]],state.integers[c.operands[2]]});return Reply{};
    }});
    bind(145,{4,0,[this](const Invocation& c){
        Message message{};for(unsigned i=0;i<3;++i){const auto value=c.arguments[i+1];
            if(!std::isfinite(value) || double(value)<std::numeric_limits<std::int32_t>::min() || double(value)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Invalid worker message");
            message[i]=std::int32_t(value);
        }
        return Reply{false,post(worker_slot(c.arguments[0]),message)?1.f:0.f};
    }});
    bind(147,{0,3,[this](const Invocation& c){
        auto& queue=workers_.at(*current_).messages;if(queue.empty())return Reply{true,{}};
        const auto message=queue.front();queue.pop_front();auto& state=current_state();
        for(unsigned i=0;i<3;++i)state.integers[c.operands[i]]=message[i];return Reply{};
    }});
    bind(146,{0,4,[this](const Invocation& c){
        auto& queue=workers_.at(*current_).messages;auto& state=current_state();
        state.integers[c.operands[0]]=queue.empty()?0:1;
        if(!queue.empty()){const auto message=queue.front();queue.pop_front();for(unsigned i=0;i<3;++i)state.integers[c.operands[i+1]]=message[i];}
        return Reply{};
    }});
    bind(148,{0,1,[this](const Invocation& c){workers_.at(worker_slot(float(current_state().integers[c.operands[0]]))).messages.clear();return Reply{};}});
    bind(94,{0,1,[this](const Invocation& c){current_state().integers[c.operands[0]]=free_slot();return Reply{};}});
    bind(96,{0,2,[this](const Invocation& c){replace(worker_slot(float(c.operands[1])),c.operands[0]);return Reply{};}});
    bind(97,{0,2,[this](const Invocation& c){replace(worker_slot(float(current_state().integers[c.operands[1]])),c.operands[0]);return Reply{};}});
    bind(99,{0,1,[this](const Invocation& c){stop(worker_slot(float(c.operands[0])),false);return Reply{};}});
    bind(100,{0,1,[this](const Invocation& c){stop(worker_slot(float(current_state().integers[c.operands[0]])),false);return Reply{};}});
    bind(102,{1,0,[this](const Invocation& c){const auto slot=worker_slot(c.arguments[0]);stop(slot);return Reply{false,float(slot)};}});
    bind(103,{0,0,[this](const Invocation&){
        for(unsigned slot=0;slot<workers_.size();++slot)if(current_!=slot){stop(slot);workers_[slot].reserved=false;}
        return Reply{};
    }});
    bind(104,{0,1,[this](const Invocation& call){dispatch(call.operands[0]);return Reply{};}});
    // 13e7f8 stops only the first matching event in slots 0..22.
    bind(106,{0,1,[this](const Invocation& c){for(unsigned slot=0;slot<23;++slot)if(workers_[slot].event==c.operands[0]){stop(slot,false);break;}return Reply{};}});
    bind(107,{0,0,[this](const Invocation&){exclusive_=current_;return Reply{};}});
    bind(108,{0,0,[this](const Invocation&){exclusive_.reset();return Reply{};}});
    // Reservation prevents automatic allocation, not execution. The original
    // update loop tests +334 (running), never +394 (reserved).
    bind(126,{0,1,[this](const Invocation& c){workers_.at(worker_slot(float(c.operands[0]))).reserved=true;return Reply{};}});
    bind(127,{0,1,[this](const Invocation& c){workers_.at(worker_slot(float(current_state().integers[c.operands[0]]))).reserved=true;return Reply{};}});
    bind(128,{1,0,[this](const Invocation& c){workers_.at(worker_slot(c.arguments[0])).reserved=false;return Reply{false,0.f};}});
    bind(129,{0,0,[this](const Invocation&){for(auto& worker:workers_)worker.reserved=false;return Reply{false,0.f};}});
    bind(160,{1,0,[this](const Invocation& c){return Reply{false,active(worker_slot(c.arguments[0]))?1.f:0.f};}});
    // 390/450/4b0/510/570/5d0 are the final entries of the ordinary
    // worker arrays: character commands execute in reserved worker 23.
    bind(457,{0,1,[this](const Invocation& c){command(c,c.operands[0]);return Reply{};}});
    bind(458,{0,1,[this](const Invocation& c){const auto id=c.operands[0];const auto entry=events_.entry(id);if(!entry)throw std::runtime_error("Command event has no authored entry");command(c,*entry,id);return Reply{};}});
    bind(459,{0,1,[this](const Invocation& c){const auto id=current_state().integers[c.operands[0]];const auto entry=events_.entry(unsigned(id));if(!entry)throw std::runtime_error("Command event has no authored entry");command(c,*entry,unsigned(id));return Reply{};}});
    bind(460,{2,0,[this](const Invocation& c){
        const auto address=c.arguments[0],parameter=c.arguments[1];
        if(!std::isfinite(address) || address<0 || double(address)>=c.program.size())throw std::runtime_error("Command entry outside script");
        if(!std::isfinite(parameter) || double(parameter)<std::numeric_limits<std::int32_t>::min() || double(parameter)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Command parameter outside integer range");
        command(c,std::uint32_t(address));workers_[23].state.integers[0]=std::int32_t(parameter);return Reply{false,0.f};
    }});
    bind(461,{0,0,[this](const Invocation& c){reset_command(c);return Reply{};}});
    bind(462,{0,1,[this](const Invocation& c){current_state().integers[c.operands[0]]=active(23)?1:0;return Reply{};}});
    // 12eb60 / 12ebf0 set and read the shared scene-number field; they do
    // not load another program or terminate the current process.
    bind(523,{1,0,[this](const Invocation& call){
        const auto n=call.arguments[0];if(!std::isfinite(n) || double(n)<std::numeric_limits<std::int32_t>::min() || double(n)>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("Invalid script scene number");
        scene_number_=std::int32_t(n);return Reply{false,float(scene_number_)};
    }});
    bind(524,{0,0,[this](const Invocation&){return Reply{false,float(scene_number_)};}});
    for(unsigned opcode:{1116u,1117u,1120u,1121u})bind(opcode,{opcode==1121?3u:opcode==1120?2u:1u,0,[this,opcode](const Invocation& c){
        const float pointer=c.arguments[0];
        if(!std::isfinite(pointer)||pointer<0||double(pointer)>=c.program.size())throw std::runtime_error("Invalid saved context name");
        const auto name=c.program.string(unsigned(pointer));
        if(!store_context_||!load_context_)throw std::runtime_error("Game context resource storage is not connected");
        if(opcode==1116){
            capture_context(name);return Reply{false,1.f};
        }
        if(opcode==1117)return Reply{false,restore_context(name)?1.f:0.f};
        const auto stored=load_context_(name);
        if(!stored)return Reply{false,opcode==1120||opcode==1121?-1.f:0.f};
        StateReader input(*stored);if(input.word()!=0x43534c4c||input.word()!=1)throw std::runtime_error("Unsupported native game context");
        const auto scene=input.word(),ticks=input.word();
        if(opcode==1120){
            const auto mode=c.arguments[1];if(!std::isfinite(mode)||mode<0||mode>=3)return Reply{false,-1.f};
            if(int(mode)==1)return Reply{false,float(scene)};
            if(int(mode)==2)return Reply{false,float(ticks)};
            throw std::runtime_error("Saved context header query requires the save-file service");
        }
        const auto count=input.word();if(count>32768)throw std::runtime_error("Game context flag count is invalid");
        std::map<std::uint32_t,bool> flags;
        for(unsigned i=0;i<count;++i){const auto id=input.word(),value=input.word();if(id>=32768||value>1||!flags.emplace(id,value!=0).second)throw std::runtime_error("Invalid game context flag");}
        const auto inventory=input.bytes(input.word());if(!input.empty())throw std::runtime_error("Trailing game context data");
        if(opcode==1121){
            if(!saved_inventory_value_)throw std::runtime_error("Saved inventory query owner is not connected");
            for(auto value:c.arguments.subspan(1))if(!std::isfinite(value)||double(value)<INT32_MIN||double(value)>=2147483648.)throw std::runtime_error("Invalid saved inventory selector");
            return Reply{false,float(saved_inventory_value_(inventory,int(c.arguments[1]),int(c.arguments[2])))};
        }
        throw std::logic_error("Unexpected context operation");
    }});
    // 141ed8 prints an inline script string using the fixed format "%s\n".
    bind(676,{0,1,[this](const Invocation& call){const auto message=call.program.string(call.operands[0]);if(trace_)trace_(message);return Reply{};}});
    bind(677,{0,1,[this](const Invocation& call){const auto value=current_state().integers[call.operands[0]];if(trace_)trace_("local["+std::to_string(call.operands[0])+"] = "+std::to_string(value));return Reply{};}});
    bind(678,{0,1,[this](const Invocation& call){const auto value=current_state().floats[call.operands[0]];if(trace_)trace_("float["+std::to_string(call.operands[0])+"] = "+std::to_string(value));return Reply{};}});
    bind(683,{1,0,[this](const Invocation& call){
        const float address=call.arguments[0];if(!std::isfinite(address) || address<0 || address!=std::floor(address) || double(address)>=call.program.size())throw std::runtime_error("Invalid script diagnostic string");
        const auto message=call.program.string(std::uint32_t(address));if(trace_)trace_(message);return Reply{false,0.f};
    }});
    if(entry==Entry::startup)start(0,events_.startup());
}
const Program& Process::current_program() const {return *groups_.at(current_?workers_[*current_].group:default_group_).program;}
std::int32_t Process::load_script(const std::string& name){
    const auto slot=free_slot();if(slot<0)return -1;
    if(!file_loader_)throw std::runtime_error("Installed script loader is not connected");
    auto found=group_names_.find(name);
    std::int32_t handle;
    if(found==group_names_.end()){
        auto program=std::make_shared<Program>(name,file_loader_(name));
        handle=next_group_++;groups_.emplace(handle,ScriptGroup{std::move(program),0,false});group_names_.emplace(name,handle);
    }else handle=found->second;
    auto& group=groups_.at(handle);
    if(!group.registered){
        unsigned count=0;for(const auto& [id,g]:groups_)count+=g.registered;
        if(count>=24)throw std::runtime_error("No free script group record");
        group.registered=true;group.result=0;
    }
    start(unsigned(slot),0,{},handle);return handle;
}
void Process::finish_group(std::int32_t result){
    if(!current_)throw std::logic_error("No executing script group");
    const auto handle=workers_[*current_].group;
    complete_scene(handle,result);
}
void Process::complete_scene(std::int32_t handle,std::int32_t result){
    (void)groups_.at(handle);
    for(unsigned slot=0;slot<workers_.size();++slot)if(workers_[slot].group==handle){stop(slot);if(slot==23)release_command_owner();}
    groups_.at(handle).result=result;
}
std::int32_t Process::start_scene(const std::string& name){
    // kernel.cb pairs event-table operation 143 with each scene load (137).
    // A new scene archive load supplies pristine CB storage. Cached decoded
    // programs may survive, but script globals and allocations must not retain
    // the preceding visit's worker handles and initialization flags.
    if(const auto found=group_names_.find(name);found!=group_names_.end()){
        auto& group=groups_.at(found->second);
        if(group_running(found->second))throw std::logic_error("Cannot restart a running scene");
        group.data=std::make_shared<std::map<std::uint32_t,float>>();
        group.arrays=std::make_shared<std::map<std::uint32_t,std::uint32_t>>();
    }
    const auto handle=load_script(name);if(handle<0)return handle;
    const auto dot=name.rfind('.');
    events_=EventTable(*groups_.at(handle).program,file_loader_(name.substr(0,dot)+".et"));
    default_group_=handle;
    return handle;
}
void Process::retire_scene(std::int32_t handle){
    if(group_running(handle))throw std::logic_error("Cannot retire a running scene");
    groups_.at(handle).registered=false;
}

void Process::capture_context(const std::string& name){
    if(!store_context_)throw std::runtime_error("Game context storage is not connected");
    store_context_(name,context_snapshot());
}
std::vector<std::uint8_t> Process::context_snapshot()const{
    if(!capture_inventory_)throw std::runtime_error("Game context requires inventory checkpoint owner");
    StateWriter output;output.word(0x43534c4c);output.word(1);output.word(std::uint32_t(scene_number_));output.word(play_ticks_);
    const auto flags=flags_.snapshot();output.word(unsigned(flags.size()));
    for(const auto& [id,value]:flags){output.word(id);output.word(value?1:0);}
    const auto inventory=capture_inventory_();output.word(unsigned(inventory.size()));output.bytes(inventory);
    return output.take();
}
bool Process::restore_context(const std::string& name){
    if(!load_context_)throw std::runtime_error("Game context storage is not connected");
    const auto stored=load_context_(name);if(!stored)return false;
    StateReader input(*stored);if(input.word()!=0x43534c4c||input.word()!=1)throw std::runtime_error("Unsupported native game context");
    const auto scene=input.word(),ticks=input.word();
    const auto count=input.word();if(count>32768)throw std::runtime_error("Game context flag count is invalid");
    std::map<std::uint32_t,bool> flags;
    for(unsigned i=0;i<count;++i){const auto id=input.word(),value=input.word();if(id>=32768||value>1||!flags.emplace(id,value!=0).second)throw std::runtime_error("Invalid game context flag");}
    const auto inventory=input.bytes(input.word());if(!input.empty())throw std::runtime_error("Trailing game context data");
    if(!restore_inventory_)throw std::runtime_error("Game context inventory restoration is not connected");
    restore_inventory_(inventory);flags_.restore(flags);scene_number_=std::bit_cast<std::int32_t>(scene);play_ticks_=ticks;
    return true;
}
void Process::start(unsigned slot,std::uint32_t address,std::optional<unsigned> event,std::optional<std::int32_t> group){
    auto& worker=workers_.at(slot);if(worker.runtime)throw std::logic_error("Script slot already running");
    // 13dde8 resets execution stacks but leaves the slot's local banks intact.
    worker.state.flags=flags_;worker.event=event;
    worker.state.expression_cells=expression_cells_;worker.state.expression_worker=slot;
    worker.messages.clear();
    worker.group=group.value_or(current_?workers_[*current_].group:default_group_);
    worker.state.data=groups_.at(worker.group).data;worker.state.arrays=groups_.at(worker.group).arrays;
    worker.runtime=std::make_unique<Runtime>(groups_.at(worker.group).program,worker.state,bindings_,address);
    worker.running=true;
}
State& Process::current_state(){if(!current_)throw std::logic_error("No executing script worker");return workers_[*current_].state;}
void Process::reset_command(const Invocation& call){
    // 12d770 also stops actor translation and heading control. Require the
    // native services instead of silently accepting a visual no-op.
    for(const auto opcode:{428u,442u}){
        const auto found=bindings_.find(opcode);
        if(found==bindings_.end())throw std::runtime_error("Command reset requires actor motion binding "+std::to_string(opcode));
        if(found->second.arguments || found->second.operands)throw std::logic_error("Invalid actor motion stop binding");
    }
    for(const auto opcode:{428u,442u}){
        const auto reply=bindings_.at(opcode).invoke({call.program,call.address,opcode,{},{}});
        if(reply.waiting || reply.value)throw std::logic_error("Actor motion stop must complete without a value");
    }
    stop(23,false);
    release_command_owner();
}
void Process::command_owner(State* state){
    for(unsigned i=0;i<workers_.size();++i)if(&workers_[i].state==state){command_owner_=i;return;}
    throw std::runtime_error("Actor command owner does not belong to this process");
}
void Process::release_command_owner(){
    if(referenced_command_owner_ && workers_[*referenced_command_owner_].command_references)--workers_[*referenced_command_owner_].command_references;
    referenced_command_owner_.reset();
}
void Process::dispatch_command(unsigned event){
    if(current_)throw std::logic_error("Automatic actor dispatch during worker execution");
    const auto entry=events_.entry(event);if(!entry)throw std::runtime_error("Actor action has no authored event entry: "+std::to_string(event));
    if(active(23))reset_command({*program_,0,458,{},{}});
    const auto group=command_owner_?workers_[*command_owner_].group:1;
    if(*entry>=groups_.at(group).program->size())throw std::runtime_error("Actor command entry outside owner script");
    stop(23);start(23,*entry,event,group);
    if(command_owner_ && *command_owner_!=23 && active(*command_owner_)){referenced_command_owner_=command_owner_;++workers_[*command_owner_].command_references;}
}
void Process::command(const Invocation& call,std::uint32_t address,std::optional<unsigned> event){
    if(address>=call.program.size())throw std::runtime_error("Command entry outside script");
    if(active(23))reset_command(call);
    command_owner_=current_;
    replace(23,address,event);
    if(current_ && *current_!=23){referenced_command_owner_=current_;++workers_[*current_].command_references;}
}
int Process::free_slot()const{
    for(unsigned slot=0;slot<23;++slot)if(!workers_[slot].running && !workers_[slot].reserved)return int(slot);
    return -1;
}
void Process::stop(unsigned slot,bool interrupt_current){
    auto& worker=workers_.at(slot);worker.running=false;worker.event.reset();
    if(current_==slot && worker.runtime){if(interrupt_current)worker.runtime->interrupt();}else worker.runtime.reset();
}
void Process::replace(unsigned slot,std::uint32_t address,std::optional<unsigned> event){
    if(address>=current_program().size())throw std::runtime_error("Worker entry outside script");
    auto& worker=workers_.at(slot);
    if(current_==slot){
        // 13dde8 changes the saved slot PC, not the currently executing PC.
        // 101dd4 writes the current PC back at yield, overwriting that entry.
        // A self-targeted restart therefore continues its current frame/tail.
        worker.running=true;worker.event=event;return;
    }
    stop(slot);
    start(slot,address,event);
}
unsigned Process::spawn(std::uint32_t address){
    // 13d630..13d688 scans slots 0..22. Slot 23 is reserved.
    const auto slot=free_slot();if(slot>=0){if(current_==unsigned(slot))replace(unsigned(slot),address);else start(unsigned(slot),address);return unsigned(slot);}
    throw std::runtime_error("No free original script worker slot");
}
unsigned Process::spawn_from(unsigned worker,std::uint32_t address){
    const auto group=workers_.at(worker).group;const auto slot=free_slot();
    if(slot<0)throw std::runtime_error("No free original script worker slot");
    start(unsigned(slot),address,{},group);return unsigned(slot);
}
bool Process::battle_target_command_ready(std::int32_t group)const{
    const auto found=groups_.find(group);if(found==groups_.end())return true;
    const auto& script=found->second;
    for(const auto& [entry,helper]:script.program->battle_helpers())if(helper.first==0){
        // Shared battle state precedes the three 36-word enemy records.
        // Its selected enemy (0) and actor command mode (3) are the actual
        // conditions checked by the battle command receiver, not HUD state.
        const auto cell=[&](unsigned word){const auto it=script.data->find(word);return it==script.data->end()?std::bit_cast<float>(script.program->word(word)):it->second;};
        bool living=false;
        for(unsigned enemy=0;enemy<3;++enemy){const auto status=cell(helper.second+enemy*36+3);living|=status!=-1&&status!=-2&&status!=-3&&status!=-4;}
        if(!living)return true; // Never hold later input behind a dead encounter.
        const auto selected=cell(helper.second-13);
        const auto mode=cell(helper.second-10);
        // The receiver's mode-3 and mode-5 branches use ANY recognized word
        // to interrupt the hold (actor messages 23/30). Do not delay escape.
        if(mode==3||mode==5)return true;
        return selected>=0&&selected<3&&mode==0;
    }
    return true;
}
bool Process::post(unsigned slot,Message message){
    auto& queue=workers_.at(slot).messages;
    // 1389b8: full queues reject the new message, preserving all 64 entries.
    if(queue.size()==64)return false;queue.push_back(message);return true;
}
unsigned Process::dispatch(unsigned event){
    const auto entry=events_.entry(event);if(!entry)throw std::runtime_error("Requested event has no authored entry");
    const auto slot=spawn(*entry);workers_[slot].event=event;return slot;
}
unsigned Process::dispatch_from(unsigned worker,unsigned event){
    const auto entry=events_.entry(event);
    if(!entry)throw std::runtime_error("Analysis event has no authored entry in its originating script");
    const auto slot=spawn_from(worker,*entry);workers_[slot].event=event;return slot;
}
void Process::advance(){
    if(current_)throw std::logic_error("Recursive script process update");
    try{
        // 101b48..101e78 visits all 24 slots in ascending order. Workers spawned
        // into a later slot are therefore eligible during this same update.
        for(unsigned slot=0;slot<workers_.size();++slot){
            if(!workers_[slot].running || (exclusive_ && *exclusive_!=slot))continue;
            current_=slot;
            auto& worker=workers_[slot];
            if(worker.runtime->advance()==Result::complete || !worker.running){worker.runtime.reset();worker.running=false;worker.event.reset();if(slot==23)release_command_owner();}
        }
        current_.reset();
        if(command_timer_>=0)command_timer_+=1.f;
        if(play_ticks_<=0x0cdfe5ffu)++play_ticks_;
    }catch(const std::exception& error){
        const auto slot=current_;current_.reset();
        if(slot)throw std::runtime_error(std::string(error.what())+" [worker "+std::to_string(*slot)+"]");
        throw;
    }catch(...){current_.reset();throw;}
}
std::size_t Process::active_count() const {std::size_t count=0;for(const auto& w:workers_)count+=w.running;return count;}
}
