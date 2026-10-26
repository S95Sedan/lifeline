#pragma once
#include "common.hpp"

namespace lifeline::setup {
void convert_movie(const fs::path &iso, const fs::path &output, const std::string &name,
                   const std::string &archive, const std::string &member);
}
