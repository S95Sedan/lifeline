#pragma once
#include "script/runtime.hpp"
#include "script/curves.hpp"
#include <array>
#include <bit>
#include <deque>

namespace lifeline::script {
// Original cooperative worker slots, backed by native bindings and script data.
class Process {
public:
    using Message=std::array<std::int32_t,3>;
    using FileLoader=std::function<std::vector<std::uint8_t>(const std::string&)>;
private:
    std::shared_ptr<const Program> program_;
    EventTable events_;
    FlagBank flags_;
    // Shared script-context signals (13f518..13fa2c), used across workers.
    std::array<State::Signal,16> signals_{};
    std::shared_ptr<std::array<float,24*128>> expression_cells_=std::make_shared<std::array<float,24*128>>();
    Bindings bindings_;
    Curves curves_;
    struct ScriptGroup {
        std::shared_ptr<const Program> program;
        std::int32_t result=0;
        bool registered=true;
        std::shared_ptr<std::map<std::uint32_t,float>> data=std::make_shared<std::map<std::uint32_t,float>>();
        std::shared_ptr<std::map<std::uint32_t,std::uint32_t>> arrays=std::make_shared<std::map<std::uint32_t,std::uint32_t>>();
    };
    std::map<std::int32_t,ScriptGroup> groups_;
    std::map<std::string,std::int32_t> group_names_;
    std::int32_t next_group_=2;
    std::int32_t default_group_=1;
    FileLoader file_loader_;
    struct Worker {
        State state;
        std::unique_ptr<Runtime> runtime;
        std::optional<unsigned> event;
        bool running=false,reserved=false;
        unsigned command_references=0;
        std::int32_t group=1;
        std::deque<Message> messages;
    };
    std::array<Worker,24> workers_;
    std::optional<unsigned> current_,exclusive_;
    std::optional<unsigned> command_owner_,referenced_command_owner_;
    std::int32_t scene_number_=0;
    std::uint32_t play_ticks_=0;
    std::function<std::vector<std::uint8_t>()> capture_inventory_;
    std::function<void(std::span<const std::uint8_t>)> restore_inventory_;
    std::function<std::int32_t(std::span<const std::uint8_t>,int,int)> saved_inventory_value_;
    std::function<void(const std::string&,std::vector<std::uint8_t>)> store_context_;
    std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)> load_context_;
    // Preserved process option written by 365. Its gameplay consumer has not
    // been identified; do not repurpose it as a native platform/render switch.
    bool scene_script_option_=false;
    float command_timer_=-1;
    std::function<void(std::string_view)> trace_;
    void start(unsigned slot,std::uint32_t address,std::optional<unsigned> event={},std::optional<std::int32_t> group={});
    const Program& current_program() const;
    std::int32_t load_script(const std::string&);
    void finish_group(std::int32_t);
    void replace(unsigned slot,std::uint32_t address,std::optional<unsigned> event={});
    void reset_command(const Invocation&);
    void release_command_owner();
    void command(const Invocation&,std::uint32_t address,std::optional<unsigned> event={});
    State& current_state();
    int free_slot() const;
public:
    enum class Entry { startup, manual };
    Process(std::shared_ptr<const Program>,EventTable,Bindings,Entry=Entry::startup,std::function<void(std::string_view)> trace={});
    Process(const Process&)=delete;
    Process& operator=(const Process&)=delete;
    unsigned spawn(std::uint32_t address);
    unsigned spawn_from(unsigned worker,std::uint32_t address);
    unsigned dispatch(unsigned event);
    unsigned dispatch_from(unsigned worker,unsigned event);
    void stop(unsigned slot,bool interrupt_current=true);
    void advance();
    void command_owner(State*);
    void dispatch_command(unsigned event);
    void file_loader(FileLoader loader){file_loader_=std::move(loader);}
    void inventory_checkpoint(std::function<std::vector<std::uint8_t>()> capture,std::function<void(std::span<const std::uint8_t>)> restore){capture_inventory_=std::move(capture);restore_inventory_=std::move(restore);}
    void context_storage(std::function<void(const std::string&,std::vector<std::uint8_t>)> store,std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)> load){store_context_=std::move(store);load_context_=std::move(load);}
    void saved_inventory_query(std::function<std::int32_t(std::span<const std::uint8_t>,int,int)> query){saved_inventory_value_=std::move(query);}
    std::int32_t start_external(const std::string& name){return load_script(name);}
    std::int32_t start_scene(const std::string& name);
    void complete_scene(std::int32_t handle,std::int32_t result);
    void retire_scene(std::int32_t handle);
    std::int32_t group_result(std::int32_t group)const{return groups_.at(group).result;}
    float inspect_data(std::int32_t group,std::uint32_t word)const{
        const auto& g=groups_.at(group);const auto found=g.data->find(word);
        return found!=g.data->end()?found->second:std::bit_cast<float>(g.program->word(word));
    }
    bool group_running(std::int32_t group)const{
        for(const auto& worker:workers_)if(worker.running&&worker.group==group)return true;
        return false;
    }
    bool post(unsigned slot,Message);
    bool battle_target_command_ready(std::int32_t group)const;
    bool title_back(){for(auto& worker:workers_)if(worker.running&&worker.runtime->title_back())return true;return false;}
    FlagBank& flags(){return flags_;}
    bool active(unsigned slot) const {return workers_.at(slot).running;}
    std::size_t active_count() const;
    std::vector<std::pair<std::string,std::uint32_t>> locations()const{
        std::vector<std::pair<std::string,std::uint32_t>> result;
        for(const auto& worker:workers_)if(worker.running)result.emplace_back(groups_.at(worker.group).program->name(),worker.runtime->address()*4);
        return result;
    }
    std::int32_t scene_number() const {return scene_number_;}
    void capture_context(const std::string& name);
    std::vector<std::uint8_t> context_snapshot()const;
    bool restore_context(const std::string& name);
    bool scene_script_option()const{return scene_script_option_;}
    std::map<std::uint32_t,std::pair<unsigned,unsigned>> binding_shapes()const{
        std::map<std::uint32_t,std::pair<unsigned,unsigned>> result;
        for(const auto& [opcode,binding]:bindings_)result.emplace(opcode,std::pair{binding.arguments,binding.operands});
        return result;
    }
#ifdef LIFELINE_DEV_SCENE_PROBE
    void debug_observe_bindings(std::span<const unsigned> opcodes,std::function<void(const Invocation&)> observer){
        for(auto opcode:opcodes){auto& binding=bindings_.at(opcode);auto invoke=binding.invoke;
            binding.invoke=[invoke=std::move(invoke),observer](const Invocation& call){observer(call);return invoke(call);};}
    }
#endif
};
}
