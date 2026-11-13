#pragma once
#include "script/runtime.hpp"
namespace lifeline::script {
Bindings memory_bindings();
void write_script_string(const Invocation&,float byte_address,const std::string&);
std::string read_script_string(const Invocation&,float byte_address);
}
