#include "speech/task.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace lifeline {
int pc_recognition_score(float confidence){
    if(!std::isfinite(confidence) || confidence<0 || confidence>1)throw std::runtime_error("Speech backend returned invalid confidence; release and retry");
    return int(std::lround(double(confidence)*10000));
}
std::optional<int> speech_score(const SpeechResult& result){return result.original_score?result.original_score:result.pc_score;}
std::string normalize_spoken_word(const std::string& text){
    std::string result;
    for(const unsigned char c:text){if(c>='A' && c<='Z')result+=char(c-'A'+'a');else if((c>='a' && c<='z') || (c>='0' && c<='9'))result+=char(c);else if(c!=' ' && c!='.' && c!=',' && c!='!' && c!='?' && c!=39 && c!='\t' && c!='\r' && c!='\n')return {};}
    return result;
}
bool matches_tutorial_word(const SpeechResult& r,unsigned exercise){
    constexpr const char* words[]{"hello","playstation","lifeline"};
    const auto score=speech_score(r);
    return r.error.empty() && exercise<3 && normalize_spoken_word(r.text)==words[exercise] && (!score || original_pronunciation_grade(*score)>0);
}
unsigned original_pronunciation_grade(int score){return score<4000?0:score<6000?1:score<8000?2:score<9500?3:4;}

struct SpeechTask::Impl {
    enum class Operation {begin,audio,finish,cancel};
    struct Message {Operation operation;std::uint64_t utterance=0;std::vector<std::int16_t> audio;std::shared_ptr<const SpeechContextSnapshot> context;};
    SpeechBackendConfiguration configuration;
    std::shared_ptr<const SpeechContextSnapshot> context;
    std::unique_ptr<SpeechBackend> backend;
    std::jthread worker;
    mutable std::mutex mutex;
    std::condition_variable_any changed;
    std::deque<Message> messages;
    std::optional<SpeechResult> result;
    std::atomic_bool ready_value=false,busy_value=false;
    std::atomic<std::uint64_t> valid_utterance=0;
    std::uint64_t next_utterance=1,active_utterance=0;
    std::size_t queued_samples=0;
    std::string startup_error;

    Impl(SpeechVocabulary vocabulary,SpeechMode mode,std::shared_ptr<const SpeechRecognizer> engine,
         std::optional<CommandGrammar> grammar,SpeechBackendConfiguration selected,std::uint64_t generation,
         std::uint64_t session,std::string name):configuration(std::move(selected)){
        context=std::make_shared<SpeechContextSnapshot>(SpeechContextSnapshot{generation,session,std::move(name),mode,std::move(vocabulary),std::move(grammar),std::move(engine)});
        worker=std::jthread([this](std::stop_token stop){run(stop);});
    }
    ~Impl(){worker.request_stop();changed.notify_all();if(worker.joinable())worker.join();}
    void run(std::stop_token stop){
        try{
            backend=make_original_speech_backend();backend->prepare(configuration);
            ready_value=true;changed.notify_all();
            while(!stop.stop_requested()){
                Message message;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,stop,[&]{return !messages.empty();});
                    if(stop.stop_requested())break;
                    message=std::move(messages.front());messages.pop_front();queued_samples-=message.audio.size();
                }
                try{
                    switch(message.operation){
                    case Operation::begin: backend->begin(message.utterance,message.context);break;
                    case Operation::audio: backend->accept_audio(message.audio);break;
                    case Operation::finish:
                        backend->finish_input();
                        if(auto completed=backend->poll_result()){
                            std::scoped_lock lock(mutex);
                            if(completed->utterance_id==valid_utterance.load()){result=std::move(completed);busy_value=false;}
                        }
                        changed.notify_all();break;
                    case Operation::cancel: backend->cancel(message.utterance);if(valid_utterance.load()==message.utterance)busy_value=false;changed.notify_all();break;
                    }
                }catch(const std::exception& e){
                    std::shared_ptr<const SpeechContextSnapshot> failed_context=message.context;
                    if(!failed_context){std::scoped_lock lock(mutex);failed_context=context;}
                    SpeechResult failed;failed.utterance_id=message.utterance?message.utterance:active_utterance;
                    failed.context_generation=failed_context?failed_context->generation:0;failed.session=failed_context?failed_context->session:0;failed.backend=backend->kind();
                    failed.backend_identity=backend->identity();
                    failed.status=SpeechResultStatus::error;failed.rejection=SpeechRejection::engine_rejected;failed.error=e.what();
                    {std::scoped_lock lock(mutex);if(failed.utterance_id==valid_utterance.load()){result=std::move(failed);busy_value=false;}}changed.notify_all();
                }
            }
        }catch(const std::exception& e){startup_error=e.what();ready_value=true;changed.notify_all();}
    }
    bool enqueue(Message message){
        std::scoped_lock lock(mutex);
        if(queued_samples+message.audio.size()>160000)return false;
        queued_samples+=message.audio.size();messages.push_back(std::move(message));changed.notify_all();return true;
    }
};

SpeechTask::SpeechTask(SpeechVocabulary vocabulary,SpeechMode mode,std::shared_ptr<const SpeechRecognizer> engine,
    std::optional<CommandGrammar> grammar,SpeechBackendConfiguration configuration,std::uint64_t generation,
    std::uint64_t session,std::string context_name)
    :impl_(std::make_unique<Impl>(std::move(vocabulary),mode,std::move(engine),std::move(grammar),std::move(configuration),generation,session,std::move(context_name))){}
SpeechTask::~SpeechTask(){cancel();}
bool SpeechTask::busy()const{return impl_->busy_value;}
bool SpeechTask::ready()const{return impl_->ready_value;}
void SpeechTask::configure_context(SpeechVocabulary vocabulary,SpeechMode mode,std::shared_ptr<const SpeechRecognizer> engine,
    std::optional<CommandGrammar> grammar,std::uint64_t generation,std::uint64_t session,std::string context_name){
    cancel();auto context=std::make_shared<SpeechContextSnapshot>(SpeechContextSnapshot{generation,session,std::move(context_name),mode,std::move(vocabulary),std::move(grammar),std::move(engine)});
    std::scoped_lock lock(impl_->mutex);impl_->context=std::move(context);impl_->result.reset();
}
void SpeechTask::configure_target_scope(SpeechTargetScope scope){
    std::scoped_lock lock(impl_->mutex);
    if(impl_->context->target_scope==scope)return;
    // Target presentation can change during a hit or turn. Each utterance
    // already owns an immutable context snapshot; apply the new candidate
    // scope to the next begin without cancelling audio or erasing a result.
    auto next=std::make_shared<SpeechContextSnapshot>(*impl_->context);
    next->target_scope=std::move(scope);impl_->context=std::move(next);
}
std::uint64_t SpeechTask::begin(){
    if(!ready()||busy())return 0;
    if(!impl_->startup_error.empty()){
        SpeechResult failed;failed.context_generation=impl_->context->generation;failed.session=impl_->context->session;
        failed.status=SpeechResultStatus::error;failed.rejection=SpeechRejection::unavailable;failed.error=impl_->startup_error;
        std::scoped_lock lock(impl_->mutex);impl_->result=std::move(failed);return 0;
    }
    impl_->busy_value=true;impl_->active_utterance=impl_->next_utterance++;
    impl_->valid_utterance=impl_->active_utterance;
    std::shared_ptr<const SpeechContextSnapshot> context;{std::scoped_lock lock(impl_->mutex);context=impl_->context;}
    impl_->enqueue({Impl::Operation::begin,impl_->active_utterance,{},std::move(context)});return impl_->active_utterance;
}
bool SpeechTask::accept_audio(std::span<const std::int16_t> audio){
    if(!busy()||audio.empty())return false;
    std::vector<std::int16_t> copy(audio.begin(),audio.end());
    if(impl_->enqueue({Impl::Operation::audio,impl_->active_utterance,std::move(copy),{}}))return true;
    cancel();
    SpeechResult failed;failed.utterance_id=impl_->active_utterance;failed.context_generation=impl_->context->generation;failed.session=impl_->context->session;
    failed.status=SpeechResultStatus::error;failed.rejection=SpeechRejection::overflow;failed.error="Speech audio queue overflow";
    std::scoped_lock lock(impl_->mutex);impl_->result=std::move(failed);return false;
}
void SpeechTask::finish_input(){if(busy())impl_->enqueue({Impl::Operation::finish,impl_->active_utterance,{},{}});}
void SpeechTask::submit(std::vector<std::int16_t> pcm){
    if(busy())throw std::runtime_error("Speech recognition already pending");
    if(!ready()){
        std::unique_lock lock(impl_->mutex);
        impl_->changed.wait(lock,[&]{return impl_->ready_value.load();});
    }
    if(!begin())throw std::runtime_error("Speech recognition backend is not ready");
    if(!accept_audio(pcm))return;finish_input();
}
std::optional<SpeechResult> SpeechTask::take(){std::scoped_lock lock(impl_->mutex);auto result=std::move(impl_->result);impl_->result.reset();return result;}
void SpeechTask::cancel(){
    if(!impl_)return;
    std::scoped_lock lock(impl_->mutex);
    impl_->valid_utterance=0;impl_->busy_value=false;impl_->result.reset();
    if(impl_->active_utterance){impl_->messages.push_back({Impl::Operation::cancel,impl_->active_utterance,{},{}});impl_->changed.notify_all();}
}
}
