#pragma once
#include "speech/task.hpp"
#include "speech/acoustic_model.hpp"
#include "speech/normalization.hpp"

namespace lifeline {
// Immutable disc-derived data is shared between asynchronous command attempts.
class SpeechRecognizer {
    OriginalSpeechData data_;
    SpeechNormalizationData normalization_;
    int acceptance_threshold_=4000;
    SpeechResult recognize_branch(std::span<const std::int16_t>,const SpeechVocabulary&,
        std::stop_token,SpeechMode,const CommandGrammar*,const SpeechTargetScope*,unsigned,bool*)const;
public:
    SpeechRecognizer(std::span<const unsigned char> language,std::span<const unsigned char> context,
                     std::span<const unsigned char> normalization,int acceptance_threshold=4000);
    SpeechResult recognize(std::span<const std::int16_t> pcm16000,const SpeechVocabulary& vocabulary,
                           std::stop_token stop,SpeechMode mode,const CommandGrammar* grammar=nullptr,
                           const SpeechTargetScope* target_scope=nullptr)const;
};
}
