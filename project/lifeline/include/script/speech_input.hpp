#pragma once
#include "script/runtime.hpp"
#include <array>
#include "speech/command_grammar.hpp"

namespace lifeline::script {
// Script-facing recognition latch. Acoustic recognition remains a native service.
class SpeechInput {
    bool enabled_=false;
    int device_state_=0;
    bool unavailable_feedback_enabled_=true;
    std::int32_t result_=-1;
    bool result_read_=false;
    std::int64_t gate_changes_=0;
    std::uint16_t score_=0;
    std::array<std::uint64_t,10> score_counts_{},score_sums_{};
    CommandGrammar::Parsed parsed_;
public:
    Bindings bindings();
    void device_state(int value){device_state_=value;}
    int device_state()const{return device_state_;}
    void clear_history(){score_counts_.fill(0);score_sums_.fill(0);}
    bool enabled()const{return enabled_;}
    bool unavailable_feedback_enabled()const{return unavailable_feedback_enabled_;}
    void unavailable_feedback_enabled(bool value){unavailable_feedback_enabled_=value;}
    bool submit(std::int32_t command){if(!enabled_)return false;parsed_={};result_=command;result_read_=false;return true;}
    bool submit(const CommandGrammar::Parsed& parsed){
        if(!enabled_)return false;
        // 1437a4..14382c retains categorized/sequential words even when BID
        // has no semantic rule. Combat consumes those words through opcode 626.
        // Only the -1 grammar miss becomes -4; isolated negative IDs survive.
        parsed_=parsed;result_=parsed.command==-1?-4:parsed.command;result_read_=false;return true;
    }
    std::int32_t result()const{return result_;}
    // Record once per decoder result, including rejected word hypotheses.
    // 1c800c excludes the 10000 sentinel and results without a word sequence.
    void score(std::uint16_t value,bool has_words=true){
        if(!has_words || value>=10000)return;
        // 1c800c..1c801c updates the last valid score only below the sentinel.
        score_=value;
        ++score_counts_[value/1000];score_sums_[value/1000]+=value;
    }
    // A read never consumes a result within the current process update.
    // Battle may retain an unread result across busy updates; authored 599/600
    // can still clear it immediately.
    void finish_frame(bool retain_unread=false){if(!retain_unread||result_read_)result_=-1;result_read_=false;}
};
}
