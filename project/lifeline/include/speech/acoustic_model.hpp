#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lifeline {
// Recovered ASRSPI data, not a recognizer. No host pointers or PS2 execution.
// Field names remain neutral until their consumers establish their meaning.
struct OriginalSpeechStream {
    std::uint32_t source_offset;
    std::array<std::uint32_t,6> parameters;
    std::vector<unsigned char> payload;
};
struct OriginalSpeechModel {
    std::uint16_t selector0,selector1;
    std::vector<std::uint16_t> states;
};
struct OriginalSpeechCommand {
    std::uint32_t context_id;
    std::uint16_t prefix;
    std::vector<std::uint16_t> models;
    std::uint32_t word_id=0;
};
struct SpeechCompositeTerm {
    std::uint16_t group;
    std::int16_t encoded_bias;
};
struct SpeechWarp { std::uint32_t mask; float factor; };
class OriginalSpeechData {
    std::string identity_;
    std::vector<OriginalSpeechStream> streams_;
    std::vector<SpeechWarp> warps_;
    std::vector<std::vector<std::uint16_t>> state_streams_;
    std::vector<OriginalSpeechModel> models_;
    std::vector<OriginalSpeechCommand> commands_;
    unsigned word_count_=0;
    std::vector<std::uint16_t> word_flags_;
    std::vector<std::vector<unsigned>> successors_;
    std::vector<std::vector<std::uint16_t>> acoustic_groups_;
    std::vector<std::vector<SpeechCompositeTerm>> composite_states_;
    std::uint16_t external_acoustic_row_ = 0;
    std::uint16_t hypothesis_reference_state_ = 0;
public:
    OriginalSpeechData(std::span<const unsigned char> language,
                       std::span<const unsigned char> context);
    const std::string& identity()const{return identity_;}
    const auto& streams()const{return streams_;}
    const auto& warps()const{return warps_;}
    // [state][stream], from the original ushort lookup table.
    const auto& state_streams()const{return state_streams_;}
    const auto& models()const{return models_;}
    const auto& commands()const{return commands_;}
    unsigned word_count()const{return word_count_;}
    const auto& word_flags()const{return word_flags_;}
    const auto& successors()const{return successors_;}
    const auto& acoustic_groups()const{return acoustic_groups_;}
    const auto& composite_states()const{return composite_states_;}
    std::uint16_t external_acoustic_row()const{return external_acoustic_row_;}
    std::uint16_t hypothesis_reference_state()const{return hypothesis_reference_state_;}
};
}
