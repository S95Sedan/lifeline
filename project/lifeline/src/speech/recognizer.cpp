#include "speech/recognizer.hpp"
#include "speech/frontend.hpp"
#include "speech/noise_model.hpp"
#include "speech/acoustic_states.hpp"
#include "speech/acoustic_search.hpp"
#include "speech/confidence.hpp"
#include "speech/utterance_search.hpp"
#include "speech/audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <cstdlib>
#include <limits>
#include <cctype>
#include <unordered_set>

namespace lifeline {
SpeechRecognizer::SpeechRecognizer(std::span<const unsigned char> language,std::span<const unsigned char> context,
    std::span<const unsigned char> normalization,int acceptance_threshold)
    : data_(language,context),normalization_(read_speech_normalization(std::as_bytes(normalization))),acceptance_threshold_(acceptance_threshold) {
    if(acceptance_threshold_<0||acceptance_threshold_>10000)throw std::invalid_argument("Speech acceptance threshold must be between 0 and 10000");
}
SpeechResult SpeechRecognizer::recognize(std::span<const std::int16_t> pcm,const SpeechVocabulary& vocabulary,
    std::stop_token stop,SpeechMode mode,const CommandGrammar* grammar,const SpeechTargetScope* target_scope)const {
    bool retry=false;
    auto result=recognize_branch(pcm,vocabulary,stop,mode,grammar,target_scope,7,&retry);
    if(!retry||stop.stop_requested()||!result.error.empty()||result.rejection==SpeechRejection::none)return result;
    // A changing warp can introduce discontinuities into temporal features.
    // For a near-threshold, fully covered long phrase, check the installed
    // fixed warps. Each retry must pass the same grammar, score and coverage
    // checks; never replace an already accepted result or borrow its score.
    std::optional<SpeechResult> coherent;
    for(const auto& warp:data_.warps()){
        auto alternative=recognize_branch(pcm,vocabulary,stop,mode,grammar,target_scope,warp.mask,nullptr);
        if(stop.stop_requested())return result;
        if(!alternative.error.empty()||alternative.rejection!=SpeechRejection::none||alternative.text.empty())continue;
        if(coherent&&normalize_spoken_word(coherent->text)!=normalize_spoken_word(alternative.text))return result;
        if(!coherent||alternative.original_score>coherent->original_score)coherent=std::move(alternative);
    }
    if(coherent){coherent->diagnostic+=" | Fixed-warp full-phrase retry";return std::move(*coherent);}
    return result;
}
SpeechResult SpeechRecognizer::recognize_branch(std::span<const std::int16_t> pcm,const SpeechVocabulary& vocabulary,
    std::stop_token stop,SpeechMode mode,const CommandGrammar* grammar,const SpeechTargetScope* target_scope,unsigned warp_mask,bool* retry)const {
    SpeechResult result;
    try {
        if(stop.stop_requested()) { result.error="Recognition cancelled"; return result; }
        if(pcm.size()<160 || pcm.size()>160000) throw std::invalid_argument("Speech capture must be between 10 ms and 10 seconds");
        if(vocabulary.words().size()!=data_.word_count()) throw std::runtime_error("Speech vocabulary and context do not match");
        auto paths=SpeechWordSearch::command_paths(data_);
        // Select the lesson's command set before finding acoustic boundaries.
        // All commands in that lesson compete, regardless of the current prompt.
        const auto allowed=[&](std::size_t id) {
            // A room CTX can contain a larger shared acoustic dictionary than
            // the VGP currently installed by its script.  In command mode the
            // active grammar, not every WRD row, defines the competitors for
            // this scene.  parse(string) intentionally treats the vocabulary
            // row as one component, preserving categorized combat phrases.
            if(mode==SpeechMode::commands)
                return !grammar || (id<vocabulary.words().size() && !vocabulary.words()[id].control &&
                    grammar->parse(vocabulary.words()[id].text).matched_words==1 &&
                    (!target_scope || target_scope->allows(vocabulary.words()[id].text,grammar)));
            return (mode==SpeechMode::microphone_practice && id<3) ||
                (mode==SpeechMode::movement_practice && id>=3 && id<=5) ||
                (mode==SpeechMode::destination_practice && ((id>=6 && id<=9) || id==22)) ||
                (mode==SpeechMode::combat_practice && id>=10 && id<=21);
        };
        std::vector<SpeechWordPath> words;
        std::optional<std::uint16_t> silence;
        for(std::size_t i=0;i<paths.size();++i) {
            // WRD control entries are not all silence: room vocabularies also
            // contain "..." with an ordinary multi-state acoustic path.
            // Keep that path in competition, but never emit it as a command.
            const auto word_id=data_.commands()[i].word_id;
            const auto& word=vocabulary.words()[word_id];
            if(word.control && (word.text==".S" || word.text.empty())) {
                if(paths[i].states.size()!=1 || (silence && *silence!=paths[i].states[0]))
                    throw std::runtime_error("Unsupported speech silence path");
                silence=paths[i].states[0];
            } else if(allowed(word_id)) words.push_back(std::move(paths[i]));
        }
        if(!silence) throw std::runtime_error("Missing speech silence model");
        std::vector<std::uint32_t> reference{0};
        std::unordered_map<std::uint32_t,std::size_t> word_ids;
        for(const auto& command:data_.commands())word_ids.emplace(command.context_id,command.word_id);
        std::unordered_map<std::uint32_t,std::string> cue_aliases;
        // Additional complete pronunciation, using the installed English
        // model's existing /get ready/ junction (os_9005) and /ready set/
        // continuation. It competes normally; no unmatched audio is ignored.
        if(mode==SpeechMode::commands&&data_.identity()=="Eng.USA.models.mike.1600.2.full.v2.0.0"){
            const std::vector<std::uint16_t> original{3558,3572,596,1374,1756,2774,629,4379,4356};
            constexpr std::uint16_t extended[]{3933,3948,611,2945,2588,596,1374,1756,2774,629,4379,4356};
            std::uint32_t next=0;for(const auto& c:data_.commands())next=std::max(next,c.context_id+1);
            for(const auto& command:data_.commands()){
                if(command.models!=original||!allowed(command.word_id)||
                   normalize_spoken_word(vocabulary.words()[command.word_id].text)!="readyset")continue;
                SpeechWordPath path{next++,{}};
                for(auto model:extended){const auto& states=data_.models().at(model).states;path.states.insert(path.states.end(),states.begin(),states.end());}
                word_ids.emplace(path.context_id,command.word_id);cue_aliases.emplace(path.context_id,"get ready set");words.push_back(std::move(path));
            }
            // The bomb displays PM, but players naturally expand the title.
            // Reuse the complete /Prime Minister Takayama's birth date/ path
            // already present in its alternative "Add up..." command. Keep
            // the full "Enter the sum..." prefix and its canonical script ID.
            for(const auto& command:data_.commands()){
                if(!allowed(command.word_id)||normalize_spoken_word(vocabulary.words()[command.word_id].text)!=
                    "enterthesumofthelastdigitsofpmtakayamasbirthdate"||command.models.size()<33||command.models[27]!=2508)continue;
                for(const auto& alternative:data_.commands()){
                    if(normalize_spoken_word(vocabulary.words()[alternative.word_id].text)!=
                        "addupthelastdigitsofprimeministertakayamasbirthdate"||alternative.models.size()!=command.models.size()||alternative.models[19]!=2502||
                        !std::equal(command.models.begin()+32,command.models.end(),alternative.models.begin()+32))continue;
                    std::vector<std::uint16_t> models(command.models.begin(),command.models.begin()+27);
                    models.insert(models.end(),alternative.models.begin()+19,alternative.models.end());
                    SpeechWordPath path{next++,{}};
                    for(auto model:models){const auto& states=data_.models().at(model).states;path.states.insert(path.states.end(),states.begin(),states.end());}
                    word_ids.emplace(path.context_id,command.word_id);cue_aliases.emplace(path.context_id,"Enter the sum / Prime Minister");words.push_back(std::move(path));
                }
            }
        }
        const auto word_score=[&](const SpeechUtteranceWord& word){return speech_hypothesis_score(std::uint32_t(word.cost),reference.at(word.end)-reference.at(word.begin),unsigned(word.end-word.begin));};
        const auto accept=[&](const SpeechUtteranceWord& word){return word.end>word.begin&&word_score(word)>=acceptance_threshold_;};
        std::optional<SpeechUtteranceLinks> links;
        if(mode==SpeechMode::commands){
            links.emplace();links->initial.resize(words.size());links->final.resize(words.size());links->predecessors.resize(words.size());
            for(unsigned i=0;i<words.size();++i){
                const auto id=word_ids.at(words[i].context_id);
                for(unsigned start=0;start<data_.word_count();++start)if(data_.word_flags()[start]&1){
                    const auto& next=data_.successors()[start];
                    if(std::find(next.begin(),next.end(),id)!=next.end())links->initial[i]=true;
                }
                for(auto end:data_.successors()[id])if(data_.word_flags()[end]&2)links->final[i]=true;
                for(unsigned prior=0;prior<words.size();++prior){
                    const auto& next=data_.successors()[word_ids.at(words[prior].context_id)];
                    if(std::find(next.begin(),next.end(),id)!=next.end())links->predecessors[i].push_back(prior);
                }
            }
        }
        std::unordered_map<std::uint32_t,unsigned> model_states;
        for(const auto& word:words)model_states.emplace(word.context_id,unsigned(word.states.size()));
        auto recovery_words=words;auto recovery_links=links;
        SpeechUtteranceSearch search(std::move(words),*silence,mode==SpeechMode::commands?8:1,
            mode==SpeechMode::commands?std::function<bool(const SpeechUtteranceWord&)>(accept):std::function<bool(const SpeechUtteranceWord&)>{},std::move(links));
        constexpr int recovery_threshold=1500;
        const auto recover=[&](const SpeechUtteranceWord& word){return word.end>word.begin&&word_score(word)>=recovery_threshold;};
        std::optional<SpeechUtteranceSearch> recovery;
        if(mode==SpeechMode::commands)recovery.emplace(std::move(recovery_words),*silence,8,recover,std::move(recovery_links));
        SpeechFrontend frontend(data_,normalization_,warp_mask,false);
        OriginalSpeechAcoustics acoustics(data_); SpeechNoiseModel noise(data_); SpeechAcousticStates mapping(data_);
        std::vector<std::vector<std::int16_t>> cost_history;
        unsigned active_frames=0;
        const auto consume=[&](const std::vector<SpeechFeatureFrame>& frames) {
            for(const auto& frame:frames) {
                const auto codes=acoustics.quantize(frame.features);
                const auto costs=mapping.evaluate(acoustics.state_costs(codes),{noise.push(codes,frame.active),0,0});
                reference.push_back(reference.back()+costs[data_.hypothesis_reference_state()]);
                search.push(costs);if(recovery)recovery->push(costs);active_frames+=frame.active;
                if(mode!=SpeechMode::commands) cost_history.push_back(costs);
            }
        };
        // Native band-limited 16 kHz -> 11 kHz conversion. No duration
        // stretching, peak normalization, expected-word hints or SAPI retries.
        constexpr std::size_t padding=5500;
        const auto converted=pcm.size()*11/16;
        const auto count=((padding+converted+padding+109)/110)*110;
        std::array<std::int16_t,110> block;
        constexpr double cutoff=(11000.0/16000.0)*0.94;
        for(std::size_t start=0;start<count;start+=110) {
            if(stop.stop_requested()) { result.error="Recognition cancelled"; return result; }
            block.fill(0);
            for(std::size_t i=0;i<110;++i) {
                const auto sample=start+i;
                if(sample<padding || sample>=padding+converted) continue;
                const double position=double(sample-padding)*16.0/11.0;
                const auto center=std::ptrdiff_t(position);
                double value=0,weight=0;
                for(auto j=std::max(std::ptrdiff_t(0),center-32);j<std::min(std::ptrdiff_t(pcm.size()),center+33);++j) {
                    const double delta=double(j)-position;
                    const double sinc=std::abs(delta)<1e-9 ? cutoff:std::sin(std::numbers::pi*cutoff*delta)/(std::numbers::pi*delta);
                    const double window=0.5+0.5*std::cos(std::numbers::pi*delta/33.0);
                    const double coefficient=sinc*window;
                    value+=pcm[std::size_t(j)]*coefficient; weight+=coefficient;
                }
                block[i]=std::int16_t(std::clamp(std::lround(value/weight),-32768l,32767l));
            }
            consume(frontend.push_block(block));
        }
        consume(frontend.finish());
        const auto ranked=search.ranked();
        if(!active_frames || ranked.empty() || ranked.front().total_cost>=search.silence_cost()) {
            result.rejection=active_frames ? SpeechRejection::no_word:SpeechRejection::silence;
            result.original_score=0; result.diagnostic="No command stronger than the disc's silence model"; return result;
        }
        // Endpoint eligibility is part of candidate selection. A cheap match
        // to a fragment must not hide a valid match to the entire command.
        // Preserve acoustic ordering and the existing confidence threshold.
        const auto covered=search.ranked([&](std::size_t first,std::size_t last){
            const auto begin=first*160>8000 ? first*160-8000:0;
            const auto end=std::min(pcm.size(),last*160>8000 ? last*160-8000:0);
            return speech_covers_capture(pcm,begin,end);
        });
        const auto recovery_covered=recovery?recovery->ranked([&](std::size_t first,std::size_t last){
            const auto begin=first*160>8000 ? first*160-8000:0;
            const auto end=std::min(pcm.size(),last*160>8000 ? last*160-8000:0);
            return speech_covers_capture(pcm,begin,end);
        }):std::vector<SpeechUtteranceMatch>{};
        const auto candidate_components=[&](const SpeechUtteranceMatch& candidate){
            std::vector<std::string> value;value.reserve(candidate.words.size());
            for(const auto& word:candidate.words)value.push_back(vocabulary.words()[word_ids.at(word.context_id)].text);
            return value;
        };
        const auto candidate_score=[&](const SpeechUtteranceMatch& candidate){
            // A sequence is only as reliable as its weakest acoustic word.
            // Averaging the whole span let one strong intended word subsidize
            // invented target/action words, which then executed as genuine
            // battle commands. Each boundary already has its own original
            // confidence; retain the minimum so legitimate compounds require
            // evidence for every component.
            int score=10000;
            for(const auto& word:candidate.words)score=std::min(score,int(word_score(word)));
            return score;
        };
        if(retry&&mode==SpeechMode::commands&&grammar&&covered.empty()){
            *retry=std::any_of(recovery_covered.begin(),recovery_covered.end(),[&](const auto& candidate){
                if(candidate.words.size()!=1||candidate_score(candidate)<acceptance_threshold_-1000)return false;
                const auto components=candidate_components(candidate);
                return std::count(components.front().begin(),components.front().end(),' ')>=5&&
                    grammar->parse(components).command>=0;
            });
        }
        const auto sequence_score=[&](const SpeechUtteranceMatch& candidate){
            // Replacing silence with another command model nearly always
            // lowers raw Viterbi cost, so sorting only by accumulated cost
            // systematically rewards spurious leading/trailing words.
            // Require each additional boundary to improve the weakest-word
            // confidence by ten percent of its full scale. This is a
            // vocabulary-independent sequence prior, not a phrase or scene
            // alias.
            constexpr std::int64_t boundary_cost=1000;
            return std::int64_t(candidate_score(candidate))-boundary_cost*std::int64_t(candidate.words.size()-1);
        };
        const auto state_density=[&](const SpeechUtteranceMatch& candidate){
            unsigned states=0;
            for(const auto& word:candidate.words)states+=model_states.at(word.context_id);
            // Include the complete command span, including silence-model gaps
            // between words.  Summing only word intervals lets a tiny model
            // hide most of a sentence in those gaps and appear well-sized.
            const auto frames=unsigned(candidate.end-candidate.begin);
            return frames?float(states)/float(frames):0.f;
        };
        const auto duration_adjusted_score=[&](const SpeechUtteranceMatch& candidate){
            // Disc word models normally consume about 2.5 frames per HMM
            // state. Without an occupancy term a very short noun can stretch
            // across an entire sentence and win on raw acoustic confidence.
            // Penalize distance from the observed command occupancy while
            // leaving ordinary, correctly sized winners untouched.
            return int(candidate_score(candidate))-int(std::lround(20000.f*std::abs(state_density(candidate)-.38f)));
        };
        const auto selection_score=[&](const SpeechUtteranceMatch& candidate){
            // A candidate below this occupancy has stretched too few acoustic
            // states over the utterance. Penalize only the low side: compact
            // spoken commands legitimately run faster than the model, while
            // the observed compound failure is a short model absorbing an
            // entire extra phrase.
            const auto underfill=std::max(0.f,.28f-state_density(candidate));
            std::int64_t semantic_penalty=0;
            if(grammar){
                const auto parsed=grammar->parse(candidate_components(candidate));
                semantic_penalty+=1000*std::int64_t(candidate.words.size()-std::min<std::size_t>(candidate.words.size(),parsed.matched_words));
                std::unordered_set<int> used;
                for(const auto id:parsed.categories)if(id>=0&&!used.insert(id).second)semantic_penalty+=1000;
            }
            return sequence_score(candidate)-std::int64_t(std::lround(20000.f*underfill))-semantic_penalty;
        };
        const auto candidate_tokens=[&](const SpeechUtteranceMatch& candidate){
            std::unordered_set<std::string> tokens;
            for(const auto& component:candidate_components(candidate)){
                std::string token;
                const auto flush=[&]{
                    static const std::unordered_set<std::string> function_words{"the","and","for","that","this","with","into"};
                    if(token.size()>=3&&!function_words.contains(token))tokens.insert(token);
                    token.clear();
                };
                for(const unsigned char c:component){if(std::isalnum(c))token+=char(std::tolower(c));else flush();}
                flush();
            }
            return tokens;
        };
        const auto consensus_score=[&](const SpeechUtteranceMatch& candidate){
            const auto tokens=candidate_tokens(candidate);
            if(tokens.empty())return duration_adjusted_score(candidate);
            std::int64_t support=0;
            for(const auto& token:tokens){
                int strongest=0;unsigned examined=0;
                for(const auto& alternative:recovery_covered){
                    if(&alternative==&candidate)continue;
                    if(examined++==64)break;
                    const auto other=candidate_tokens(alternative);
                    if(other.contains(token))strongest=std::max(strongest,duration_adjusted_score(alternative));
                }
                support+=std::max(0,strongest);
            }
            // Independent hypotheses that agree on different content words
            // are stronger evidence than one high-scoring stretched phrase.
            return duration_adjusted_score(candidate)+int(support/std::int64_t(tokens.size()));
        };
        const auto& eligible=covered.empty()?ranked:covered;
        const SpeechUtteranceMatch* best=&eligible.front();
        if(mode==SpeechMode::commands){
            // Raw Viterbi order remains authoritative between pronunciations
            // with the same segmentation unless a later candidate wins by a
            // full confidence decile. The correction primarily compares one
            // representative per word count; otherwise a small calibration
            // difference can turn an acoustically selected phrase such as
            // "run away" back into its fragment "run".
            std::array<const SpeechUtteranceMatch*,9> groups{};
            for(const auto& candidate:eligible){
                const auto count=candidate.words.size();
                if(count>=groups.size())continue;
                if(!groups[count]||selection_score(candidate)>=selection_score(*groups[count])+1000)groups[count]=&candidate;
            }
            for(const auto* candidate:groups)if(candidate&&selection_score(*candidate)>selection_score(*best))best=candidate;
        }
        std::string reranked_from;
        std::optional<int> recovered_confidence;
        if(grammar && mode==SpeechMode::commands){
            const auto original_components=candidate_components(*best);
            const auto original=grammar->parse(original_components);
            const auto original_score=candidate_score(*best);
            // Compound action/object phrases that resolve below 100 are the
            // authored generic refusal/recovery branches. If the acoustic
            // runner-up preserves the action and resolves to a room command,
            // prefer it only when its full-capture score is close. This uses
            // the active VGP semantics and applies to every room; it does not
            // rename a particular word or special-case a scene.
            if(!covered.empty()&&original.command>=0&&original.command<100&&original.categories[1]>=0&&original.categories[2]>=0&&original_score<8000){
                for(const auto& alternative:covered){
                    if(&alternative==best)continue;
                    const auto score=candidate_score(alternative);if(score<unsigned(acceptance_threshold_)||score+2000<original_score)continue;
                    const auto components=candidate_components(alternative);const auto parsed=grammar->parse(components);
                    if(parsed.command>=100&&parsed.categories[1]>=0&&parsed.categories[2]==original.categories[2]){
                        for(const auto& component:original_components){if(!reranked_from.empty())reranked_from+=' ';reranked_from+=component;}
                        best=&alternative;break;
                    }
                }
            }
            // A raw winner whose model occupies less than one state per four
            // frames has stretched a short command over a sentence. Re-rank
            // only that failure shape against graph-valid, full-capture VGP
            // commands from the relaxed lattice. Normal-sized commands stay
            // entirely on the strict recognizer path.
            // A valid single-word winner is already a complete command.  The
            // relaxed lattice exists to recover compound phrases whose first
            // model was stretched over the rest of the utterance; applying
            // it to a one-word winner lets unrelated long commands outvote
            // short imperatives such as Run and Right.
            // A fully consumed authored phrase is authoritative even when it
            // has no ordinary command ID. Combat targets deliberately return
            // categorized/sequence words for opcode 626, so replacing one
            // complete target with another corrupts target selection. Relaxed
            // recovery is only for a compound whose acoustic components were
            // not all consumed by the active grammar.
            if(reranked_from.empty()&&best->words.size()>1&&original.matched_words<original_components.size()&&!recovery_covered.empty()){
                const auto current_command=original.command;
                const SpeechUtteranceMatch* specific=nullptr;int specific_score=std::numeric_limits<int>::min(),current_score=std::numeric_limits<int>::min();
                for(const auto& alternative:recovery_covered){
                    const auto components=candidate_components(alternative);const auto parsed=grammar->parse(components);
                    if(parsed.command<100)continue;
                    const auto adjusted=consensus_score(alternative);
                    if(parsed.command==current_command)current_score=std::max(current_score,adjusted);
                    if(adjusted>specific_score){specific=&alternative;specific_score=adjusted;}
                }
                const bool stretched=covered.empty()||state_density(*best)<.26f;
                if(specific&&specific!=best&&specific_score>=1000&&(stretched||specific_score>=current_score+250)){
                    for(const auto& component:original_components){if(!reranked_from.empty())reranked_from+=' ';reranked_from+=component;}
                    best=specific;recovered_confidence=original_score;
                }
            }
        }
        std::vector<std::string> components;std::string recognized;
        for(const auto& word:best->words){
            const auto& text=vocabulary.words()[word_ids.at(word.context_id)].text;
            components.push_back(text);if(!recognized.empty())recognized+=' ';recognized+=text;
        }
        const auto acoustic_score=candidate_score(*best);
        const auto score=std::max(acoustic_score,recovered_confidence.value_or(0));
        const auto command=word_ids.find(best->context_id);
        const auto id=command==word_ids.end()?vocabulary.words().size():command->second;
        if(id>=vocabulary.words().size()) throw std::runtime_error("Unknown recognized speech context ID");
        std::ostringstream diagnostic;
        diagnostic<<"Native disc model | candidate "<<recognized<<" | word frames "<<best->begin<<".."<<best->end
                  <<" | warp "<<data_.warps()[frontend.selected_branch()].factor<<" | score "<<score;
        if(recovered_confidence)diagnostic<<" | recovered candidate acoustic score "<<acoustic_score;
        if(!reranked_from.empty())diagnostic<<" | grammar reranked from "<<reranked_from;
        if(std::getenv("LIFELINE_SPEECH_LATTICE")){
            for(const auto& alternative:ranked){
                if(alternative.words.size()!=1)continue;
                diagnostic<<" | path "<<vocabulary.words()[word_ids.at(alternative.context_id)].text
                    <<'='<<candidate_score(alternative)<<'@'<<alternative.begin<<".."<<alternative.end;
            }
            diagnostic<<" | winner-density "<<state_density(*best)<<" adjusted "<<duration_adjusted_score(*best);
            diagnostic<<" sequence "<<sequence_score(*best);
            unsigned shown=0;
            for(const auto& alternative:recovery_covered){
                if(shown++==20)break;
                const auto parts=candidate_components(alternative);diagnostic<<" | lattice";
                for(std::size_t i=0;i<parts.size();++i)diagnostic<<' '<<parts[i]<<'~'<<word_score(alternative.words[i]);
                diagnostic<<'='<<candidate_score(alternative)<<'@'<<state_density(alternative)<<':'<<duration_adjusted_score(alternative)<<':'<<sequence_score(alternative);
                if(grammar){const auto parsed=grammar->parse(parts);diagnostic<<'#'<<parsed.command<<'/'<<parsed.matched_words<<'[';
                    for(auto category:parsed.categories)diagnostic<<category<<',';diagnostic<<']';}
            }
        }
        result.diagnostic=diagnostic.str(); result.original_score=int(score);
        for(const auto& word:best->words)if(const auto alias=cue_aliases.find(word.context_id);alias!=cue_aliases.end())result.diagnostic+=" | pronunciation: "+alias->second;
        // Keep the runner-up evidence in rejection logs: a wrong winning
        // label alone cannot distinguish bad acoustics from bad endpoints.
        if(mode==SpeechMode::movement_practice){
            for(const auto& alternative:ranked){
                if(alternative.context_id==best->context_id)continue;
                const auto found=std::find_if(data_.commands().begin(),data_.commands().end(),[&](const auto& item){return item.context_id==alternative.context_id;});
                const auto other=found==data_.commands().end()?vocabulary.words().size():found->word_id;
                if(other>=vocabulary.words().size())throw std::runtime_error("Unknown alternative speech context ID");
                const auto value=speech_hypothesis_score(std::uint32_t(alternative.word_cost),reference[alternative.end]-reference[alternative.begin],unsigned(alternative.end-alternative.begin));
                const bool full=std::any_of(covered.begin(),covered.end(),[&](const auto& item){return item.context_id==alternative.context_id;});
                result.diagnostic+=" | alternative "+vocabulary.words()[other].text+" "+std::to_string(value)+" frames "+std::to_string(alternative.begin)+".."+std::to_string(alternative.end)+(full?" covered":" incomplete");
            }
        }
        if(!covered.empty() && best->context_id!=ranked.front().context_id)
            result.diagnostic+=" | Selected full-capture match after excluding incomplete candidates";
        if(best->total_cost>=search.silence_cost()) {
            result.rejection=SpeechRejection::no_word;
            result.diagnostic+=" | Rejected: full-capture match weaker than silence";return result;
        }
        if(score<unsigned(acceptance_threshold_)) {
            result.rejection=SpeechRejection::engine_rejected;
            result.diagnostic+=" | Rejected: score below "+std::to_string(acceptance_threshold_);
            return result;
        }
        if(mode!=SpeechMode::commands) {
            // Reject a better competing pronunciation over exactly the same
            // frames. Comparing separately endpointed hypotheses lets a longer
            // unrelated word win by absorbing surrounding background noise.
            // This is native lesson rejection policy, not recovered VGP logic.
            auto alternatives=SpeechWordSearch::command_paths(data_);
            SpeechWordSearch comparison(std::move(alternatives));
            for(auto frame=best->begin;frame<best->end;++frame) {
                if(stop.stop_requested()) { result={}; result.error="Recognition cancelled"; return result; }
                comparison.push(cost_history[frame]);
            }
            const auto competing=comparison.ranked();
            if(!competing.empty()) {
                const auto found=std::find_if(data_.commands().begin(),data_.commands().end(),[&](const auto& item){return item.context_id==competing.front().context_id;});
                const auto other=found==data_.commands().end()?vocabulary.words().size():found->word_id;
                if(other>=vocabulary.words().size()) throw std::runtime_error("Unknown competing speech context ID");
                if(!allowed(other) && competing.front().cost<best->word_cost) {
                    result.rejection=vocabulary.words()[other].control?SpeechRejection::no_word:SpeechRejection::engine_rejected;
                    result.diagnostic+=" | Rejected: stronger interval match "+vocabulary.words()[other].text;
                    return result;
                }
            }
        }
        if(covered.empty()&&!recovered_confidence) {
            result.rejection=SpeechRejection::extra_speech;
            result.diagnostic+=" | No candidate covers the detected audio"; return result;
        }
        if(std::any_of(best->words.begin(),best->words.end(),[&](const auto& word){return vocabulary.words()[word_ids.at(word.context_id)].control;})){
            result.rejection=SpeechRejection::no_word;
            result.diagnostic+=" | Non-command vocabulary entry";return result;
        }
        result.text=std::move(recognized);result.components=std::move(components);
        if(result.components.size()==1)result.word_id=unsigned(id);
    } catch(const std::exception& error) { result.error=error.what(); }
    return result;
}
}
