#pragma once
#include "script/program.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <array>

namespace lifeline::script {
class FlagBank {
    std::shared_ptr<std::map<std::uint32_t,bool>> values_=std::make_shared<std::map<std::uint32_t,bool>>();
public:
    bool& operator[](std::uint32_t index) {return (*values_)[index];}
    void clear(){values_->clear();}
    auto snapshot()const{return *values_;}
    void restore(const std::map<std::uint32_t,bool>& values){*values_=values;}
};
struct State {
    // 24 retained expression slices, 128 float cells per worker (101c48).
    // Native storage starts zeroed; BEGIN and worker replacement do not erase it.
    std::shared_ptr<std::array<float,24*128>> expression_cells=std::make_shared<std::array<float,24*128>>();
    unsigned expression_worker=0;
    struct Signal {bool allocated=false;std::uint32_t references=0;std::int32_t value=0;};
    std::map<std::uint32_t,std::int32_t> integers;
    std::map<std::uint32_t,float> floats;
    // Locals belong to a worker; copies of this bank share process flags.
    FlagBank flags;
    // Writable script-data cells shared by workers; original program stays immutable.
    std::shared_ptr<std::map<std::uint32_t,float>> data=std::make_shared<std::map<std::uint32_t,float>>();
    // Native temporary arrays use word addresses beyond the immutable CB.
    // Shared ranges allow another worker to use an array while its owner lives.
    std::shared_ptr<std::map<std::uint32_t,std::uint32_t>> arrays=std::make_shared<std::map<std::uint32_t,std::uint32_t>>();
    bool data_contains(const Program& program,std::uint32_t word,unsigned count=1)const{
        const auto end=std::uint64_t(word)+count;
        if(end<=program.size())return true;
        auto it=arrays->upper_bound(word);if(it==arrays->begin())return false;--it;
        return word>=it->first && end<=std::uint64_t(it->first)+it->second;
    }
};
enum class Result { running, yielded, waiting, complete };
struct Value {float number=0; std::uint32_t type=2,index=0;};
struct Invocation {
    const Program& program;
    std::uint32_t address,opcode;
    std::span<const float> arguments;
    std::span<const std::uint32_t> operands;
    State* state=nullptr;
};
struct Reply {bool waiting=false;std::optional<float> value;};
struct Binding {
    unsigned arguments=0,operands=0;
    std::function<Reply(const Invocation&)> invoke;
    // A few authored blocks use consecutive scalar setters as the parameters
    // of an immediately following script call.  The original unchecked stack
    // happened to preserve those values after the setters consumed them.
    bool chains_arguments=false;
};
using Bindings=std::map<std::uint32_t,Binding>;

// Executes the game's event language. No MIPS registers, console memory or devices.
class Runtime {
    std::shared_ptr<const Program> program_;
    State& state_;
    const Bindings& bindings_;
    std::uint32_t pc_;
    Result result_=Result::running;
    bool interrupted_=false;
    std::vector<Value> expression_;
    int expression_top_=0,expression_base_=0;
    std::vector<float> arguments_;
    std::vector<float> chained_arguments_;
    std::vector<float> parameters_;
    std::vector<std::uint32_t> arrays_;
    struct Saved {std::uint32_t type,index;float value;std::int32_t integer;};
    struct Frame {std::uint32_t resume;std::vector<Value> expression;std::vector<Saved> saved;std::vector<float> arguments,parameters;std::vector<std::uint32_t> arrays;int top=0,base=0;};
    std::vector<Frame> frames_;
    std::vector<Saved> saved_;
    [[noreturn]] void fail(const std::string&) const;
    Value pop();
    void push(Value);
    void jump(std::uint32_t);
    float operand(unsigned,std::uint32_t) const;
    void restore();
    void release_arrays(std::vector<std::uint32_t>&);
    Result step();
public:
    Runtime(std::shared_ptr<const Program>,State&,const Bindings&,std::uint32_t entry);
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    ~Runtime();
    // Watchdog, not an authored yield: enemy startup clears several 240-entry
    // command buffers synchronously before its first wait. Ten thousand
    // expression operations is less than that valid initialization requires.
    Result advance(unsigned instruction_budget=1000000);
    // Direct-entry resource bootstrap can execute a bounded authored block.
    void run_until(std::uint32_t boundary,unsigned instruction_budget=10000);
    // A binding may stop or replace its own worker. Defer destruction until
    // control has returned from that binding and the interpreter has unwound.
    void interrupt(){interrupted_=true;}
    bool title_back();
    std::uint32_t address() const {return pc_;}
    Result result() const {return result_;}
};
}
