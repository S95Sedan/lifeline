#pragma once

#include "speech/backend.hpp"
#include <string_view>

namespace lifeline {
struct AdaptedSpeech {
    std::string normalized;
    std::vector<std::string> components;
    bool complete=false;
    std::string diagnostic;
};

std::string normalize_transcript(std::string_view);
AdaptedSpeech adapt_transcript(std::string_view,const SpeechContextSnapshot&);
std::vector<std::string> speech_context_phrases(const SpeechContextSnapshot&);
}
