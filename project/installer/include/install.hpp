#pragma once
#include "package.hpp"
namespace lifeline::setup {
struct InstallOptions {
    fs::path iso, destination, cache_data;
    bool desktop = false, start_menu = true;
};
fs::path validate_destination(const fs::path &destination, const fs::path &iso);
Json perform_install(const InstallOptions &options, const Report &report);
Json verify_installation(const fs::path &data, const Report &report = {});
} // namespace lifeline::setup
