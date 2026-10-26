#pragma once
#include "speech/backend.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>
#include <memory>
namespace lifeline {
SpeechResult recognize_speech(std::span<const std::int16_t> pcm,const SpeechVocabulary&,std::stop_token stop={},SpeechMode mode=SpeechMode::commands);
int pc_recognition_score(float confidence);
std::optional<int> speech_score(const SpeechResult&);
std::string normalize_spoken_word(const std::string&);
bool matches_tutorial_word(const SpeechResult&,unsigned exercise);
unsigned original_pronunciation_grade(int score);
class SpeechTask {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit SpeechTask(SpeechVocabulary vocabulary,SpeechMode mode=SpeechMode::commands,
        std::shared_ptr<const SpeechRecognizer> engine={},std::optional<CommandGrammar> grammar={},
        SpeechBackendConfiguration configuration=speech_backend_configuration(),std::uint64_t generation=0,std::uint64_t session=0,std::string context_name={});
    ~SpeechTask();
    bool busy()const;
    bool ready()const;
    void configure_context(SpeechVocabulary,SpeechMode,std::shared_ptr<const SpeechRecognizer>,std::optional<CommandGrammar>,
        std::uint64_t generation,std::uint64_t session,std::string context_name);
    // Applies to subsequent utterances; does not cancel an existing capture.
    void configure_target_scope(SpeechTargetScope);
    std::uint64_t begin();
    bool accept_audio(std::span<const std::int16_t>);
    void finish_input();
    void submit(std::vector<std::int16_t>);
    std::optional<SpeechResult> take();
    void cancel();
};
}
