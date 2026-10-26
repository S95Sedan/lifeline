#pragma once

#include "speech/command_grammar.hpp"
#include "speech/vocabulary.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace lifeline {
class SpeechRecognizer;
class CombatTargets;

enum class SpeechMode { commands, microphone_practice, movement_practice, destination_practice, combat_practice };
enum class SpeechRejection { none, silence, no_word, engine_rejected, extra_speech, ambiguous, unavailable, cancelled, stale, overflow };
// A completed utterance with no dictionary match is not an empty capture.
// Scripts such as the encouragement scene wait for any spoken response,
// including -2, but opcode 597 deliberately normalizes silence (-3) to idle.
constexpr int script_speech_failure(SpeechRejection reason,bool recognized=false){
    return reason==SpeechRejection::silence?-3:recognized?-4:-2;
}
enum class SpeechBackendKind { original };
enum class SpeechResultStatus { final, cancelled, error };

struct SpeechBackendConfiguration {
    SpeechBackendKind backend=SpeechBackendKind::original;
};

// A battle selection narrows only body-part phrases. General actions and
// enemy-selection phrases remain available in the authored grammar.
struct SpeechTargetScope {
    std::vector<std::string> known_parts;
    std::vector<std::string> active_parts;
    bool enabled=false;
    bool allows(const std::string& phrase,const CommandGrammar* grammar=nullptr)const;
    bool operator==(const SpeechTargetScope&)const=default;
};
SpeechTargetScope battle_speech_target_scope(const CombatTargets&,bool panel_active);

struct SpeechContextSnapshot {
    std::uint64_t generation=0;
    std::uint64_t session=0;
    std::string name;
    SpeechMode mode=SpeechMode::commands;
    SpeechVocabulary vocabulary;
    std::optional<CommandGrammar> grammar;
    std::shared_ptr<const SpeechRecognizer> original_engine;
    SpeechTargetScope target_scope;
};

struct SpeechResult {
    std::string text,error;
    // Only the reconstructed original engine may supply this disc-model score.
    std::optional<int> original_score;
    std::optional<unsigned> word_id;
    // Canonical vocabulary/grammar boundaries consumed by GameSession::respond.
    std::vector<std::string> components;
    // A separately calibrated host score. It is never a disc acoustic score.
    std::optional<int> pc_score;
    std::string diagnostic;
    SpeechRejection rejection=SpeechRejection::none;
    bool duration_adjusted=false;
    // Lifecycle and backend metadata extends the established result without
    // changing the ordering of its original aggregate fields.
    std::uint64_t utterance_id=0;
    std::uint64_t context_generation=0;
    std::uint64_t session=0;
    SpeechBackendKind backend=SpeechBackendKind::original;
    SpeechResultStatus status=SpeechResultStatus::final;
    std::string backend_identity;
    std::string model_identity;
    std::string raw_transcript;
    std::string normalized_transcript;
    bool accepted=false;
    std::chrono::microseconds capture_duration{};
    std::chrono::microseconds decode_duration{};
};

class SpeechBackend {
public:
    virtual ~SpeechBackend()=default;
    virtual SpeechBackendKind kind()const=0;
    virtual std::string identity()const=0;
    virtual void prepare(const SpeechBackendConfiguration&)=0;
    virtual void begin(std::uint64_t utterance_id,std::shared_ptr<const SpeechContextSnapshot>)=0;
    virtual void accept_audio(std::span<const std::int16_t>)=0;
    virtual void finish_input()=0;
    virtual std::optional<SpeechResult> poll_result()=0;
    virtual void cancel(std::uint64_t utterance_id)=0;
};

std::unique_ptr<SpeechBackend> make_original_speech_backend();
SpeechBackendConfiguration speech_backend_configuration();
void speech_backend_configuration(SpeechBackendConfiguration);
const char* speech_backend_name(SpeechBackendKind);
}
