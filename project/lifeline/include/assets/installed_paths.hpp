#pragma once
#include <string>
#include <string_view>

namespace lifeline::installed {
inline std::string program(std::string_view name){return "script/programs/"+std::string(name);}
inline std::string movie(std::string_view name,std::string_view extension){return "media/movies/"+std::string(name)+std::string(extension);}
inline constexpr const char* konami_logo="ui/sequences/presentation/KOA_logo.cf";
inline constexpr const char* sony_logo="ui/sequences/presentation/complogo.cf";
inline constexpr const char* rio_model="game/actors/rio/rio_001.dat";
inline constexpr const char* rio_animation="game/actors/rio/rio_000.anm";
inline constexpr const char* security_room_rio_animation="game/scenes/013-security-room/animations/rio_013.anm";
inline constexpr const char* security_room_routes="game/scenes/013-security-room/logic/013.rs";
inline constexpr const char* security_room_model="game/scenes/013-security-room/solid013.dat";
inline constexpr const char* tutorial_score="ui/scenes/tutorial/score.cf";
inline constexpr const char* tutorial_vocabulary="speech/contexts/tutorial/vocabulary.tsv";
inline constexpr const char* tutorial_commands="speech/contexts/tutorial/commands.bin";
inline constexpr const char* speech_normalization="speech/engine/normalization.bin";
}
