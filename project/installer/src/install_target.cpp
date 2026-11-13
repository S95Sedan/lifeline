#include "install.hpp"
namespace lifeline::setup {
const char* target_name(InstallTarget target){return target==InstallTarget::steamdeck?"steamdeck":"windows";}
fs::path default_destination(InstallTarget target){
    return target==InstallTarget::steamdeck?executable_path().parent_path()/L"Steamdeck":default_destination();
}
std::optional<std::string> program_path(std::string_view name,InstallTarget target){
    constexpr std::string_view common="program/shared/";
    const std::string_view selected=target==InstallTarget::steamdeck?"program/linux/":"program/windows/";
    if(name.starts_with(common))return std::string(name.substr(common.size()));
    if(name.starts_with(selected))return std::string(name.substr(selected.size()));
    return {};
}
}
