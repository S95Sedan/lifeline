#pragma once
#include "package.hpp"
#include <optional>
namespace lifeline::setup {
enum class InstallTarget { windows, steamdeck };
const char* target_name(InstallTarget);
fs::path default_destination(InstallTarget);
std::optional<std::string> program_path(std::string_view,InstallTarget);
struct InstallOptions {
    fs::path iso, destination, cache_data;
    bool desktop = false, start_menu = true;
    InstallTarget target = InstallTarget::windows;
};
fs::path validate_destination(const fs::path &destination, const fs::path &iso);
Json perform_install(const InstallOptions &options, const Report &report);
Json verify_installation(const fs::path &data, const Report &report = {});
} // namespace lifeline::setup
