#pragma once
#include "assets/asset_directory.hpp"
#include "speech/task.hpp"
#include "speech/recording.hpp"
namespace lifeline {
void show_speech_diagnostic(const AssetDirectory&,const std::filesystem::path& output,const std::string& screenshot={},bool vent=false,bool door=false,bool bomb=false);
}
