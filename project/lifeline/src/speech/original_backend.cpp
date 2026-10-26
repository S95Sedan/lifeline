#include "speech/backend.hpp"
#include "speech/recognizer.hpp"
#include <stdexcept>

namespace lifeline {
namespace {
class OriginalBackend final:public SpeechBackend {
    std::shared_ptr<const SpeechContextSnapshot> context_;
    std::vector<std::int16_t> audio_;
    std::optional<SpeechResult> result_;
    std::stop_source stop_;
    std::uint64_t utterance_=0;
    bool cancelled_=false;
public:
    SpeechBackendKind kind()const override{return SpeechBackendKind::original;}
    std::string identity()const override{return "reconstructed-disc-model";}
    void prepare(const SpeechBackendConfiguration&)override{}
    void begin(std::uint64_t utterance,std::shared_ptr<const SpeechContextSnapshot> context)override{
        if(!context||!context->original_engine)throw std::runtime_error("Original speech task has no installed disc model");
        utterance_=utterance;context_=std::move(context);audio_.clear();result_.reset();cancelled_=false;stop_=std::stop_source();
    }
    void accept_audio(std::span<const std::int16_t> samples)override{
        if(cancelled_||!context_)return;
        audio_.insert(audio_.end(),samples.begin(),samples.end());
        if(audio_.size()>160000)throw std::runtime_error("Speech capture exceeded 10 seconds");
    }
    void finish_input()override{
        if(cancelled_||!context_)return;
        const auto started=std::chrono::steady_clock::now();
        auto result=context_->original_engine->recognize(audio_,context_->vocabulary,stop_.get_token(),context_->mode,
            context_->grammar?&*context_->grammar:nullptr,&context_->target_scope);
        result.utterance_id=utterance_;result.context_generation=context_->generation;result.session=context_->session;
        result.backend=SpeechBackendKind::original;
        result.backend_identity=identity();result.model_identity=context_->name;result.status=result.error.empty()?SpeechResultStatus::final:SpeechResultStatus::error;
        result.raw_transcript=result.text;result.normalized_transcript=result.text;result.accepted=result.error.empty()&&result.rejection==SpeechRejection::none;
        result.capture_duration=std::chrono::microseconds(audio_.size()*1000000/16000);
        result.decode_duration=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started);
        result_=std::move(result);audio_.clear();context_.reset();
    }
    std::optional<SpeechResult> poll_result()override{auto result=std::move(result_);result_.reset();return result;}
    void cancel(std::uint64_t utterance)override{if(!utterance||utterance==utterance_){stop_.request_stop();cancelled_=true;audio_.clear();context_.reset();result_.reset();}}
};
}
std::unique_ptr<SpeechBackend> make_original_speech_backend(){return std::make_unique<OriginalBackend>();}
}
