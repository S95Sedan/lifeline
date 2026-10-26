#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
namespace lifeline {
std::string speech_recording_levels(std::span<const std::int16_t>);
void save_speech_recording(const std::filesystem::path&,std::span<const std::int16_t>);
std::filesystem::path save_speech_report(const std::filesystem::path& directory,std::span<const std::int16_t>,const std::string& report);
}
