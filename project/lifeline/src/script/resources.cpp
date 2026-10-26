#include "script/resources.hpp"
#include <cmath>
#include <stdexcept>
namespace lifeline::script {
Bindings resource_bindings(Resources& resources){
    const auto name=[](const Invocation& call){const float address=call.arguments[0];
        if(!std::isfinite(address) || address<0 || double(address)>=call.program.size() || std::floor(address)!=address)throw std::runtime_error("Invalid script resource string address");
        return call.program.string(std::uint32_t(address));};
    Bindings result;
    result.emplace(158,Binding{1,0,[&resources,name](const Invocation& call){return Reply{false,float(resources.address(name(call)))};}});
    result.emplace(118,Binding{0,0,[&resources](const Invocation&){resources.checkpoint();return Reply{};}});
    result.emplace(119,Binding{0,0,[&resources](const Invocation&){resources.restore_checkpoint();return Reply{};}});
    result.emplace(112,Binding{0,1,[&resources](const Invocation& call){
        resources.load(call.program.string(call.operands[0]));return Reply{};
    }});
    result.emplace(120,Binding{1,0,[&resources,name](const Invocation& call){return Reply{false,float(resources.load(name(call)))};}});
    result.emplace(113,Binding{1,0,[&resources,name](const Invocation& call){resources.unload(name(call));return Reply{false,0.f};}});
    // Native folder reads finish synchronously; do not invent PS2 seek delays.
    result.emplace(122,Binding{0,0,[&resources](const Invocation&){return Reply{!resources.ready(),{}};}});
    return result;
}
}
