#include "speech/frontend.hpp"
#include "speech/activity.hpp"
#include "speech/branch_selector.hpp"
#include "speech/cepstral_transform.hpp"
#include "speech/energy_tracker.hpp"
#include "speech/filterbank.hpp"
#include "speech/noise_filter.hpp"
#include "speech/pcm_window.hpp"
#include "speech/spectrum.hpp"
#include "speech/temporal_features.hpp"
#include <array>
#include <bit>
#include <stdexcept>

namespace lifeline {
struct SpeechFrontend::State {
    OriginalSpeechWindow window{110,256};
    SpeechSpectrum spectrum{256,256,9,105,40000};
    SpeechEnergyTracker raw_energy{{}}, filtered_energy;
    SpeechActivityGate gate;
    SpeechAdaptationWeight adaptation;
    OriginalSpeechCepstrum cepstrum{21,13,16,1};
    OriginalSpeechFeatures temporal{12};
    SpeechBranchSelector selector;
    std::vector<SpeechFilterbank> banks;
    std::vector<SpeechNoiseFilter> noise_filters;
    std::vector<SpeechNormalization> normalizers;
    struct Filtered { std::vector<NoiseFilterFrame> branches; SpeechEnergyFrame energy; };
    std::deque<std::vector<std::vector<float>>> band_queue;
    std::deque<Filtered> filtered_queue;
    std::deque<float> weight_queue;
    std::deque<bool> activity_queue;
    std::array<std::deque<SpeechNormalizedFrame>,3> normalized_queue;
    bool finished=false;

    State(const OriginalSpeechData& data,SpeechNormalizationData normalization,std::uint32_t mask,bool relative)
        : filtered_energy([&] {
            SpeechEnergySettings settings; settings.noise_scale_parameter=2000;
            settings.channel_offset=26.524993896484375f; settings.relative_level=relative; return settings;
          }()), selector(data,mask,false) {
        if(data.warps().size()!=3) throw std::invalid_argument("Unsupported frontend warp count");
        const SpeechNormalizationSettings settings{50,15,
            6.0f*std::bit_cast<float>(0x3debc947u),-15.0f*std::bit_cast<float>(0x3debc947u),
            -30.0f*std::bit_cast<float>(0x3e6bc947u),50,5};
        for(const auto& warp:data.warps()) {
            banks.emplace_back(129u,21u,5500.0f,warp.factor);
            noise_filters.emplace_back(NoiseFilterSettings{21,3,2,0.84f,1.5f,0.02f,40,{10,16,21}});
            normalizers.emplace_back(normalization,settings);
        }
    }
    void feature(std::vector<float> values,std::vector<SpeechFeatureFrame>& output) {
        if(activity_queue.empty()) throw std::logic_error("Speech frontend activity alignment lost");
        output.push_back({std::move(values),activity_queue.front()}); activity_queue.pop_front();
    }
    void selected(std::vector<std::vector<float>> frames,std::vector<SpeechFeatureFrame>& output) {
        for(auto& frame:frames) if(auto values=temporal.push(frame)) feature(std::move(*values),output);
    }
    void normalized(std::vector<SpeechFeatureFrame>& output) {
        while(!normalized_queue[0].empty() && !normalized_queue[1].empty() && !normalized_queue[2].empty()) {
            if(weight_queue.empty()) throw std::logic_error("Speech frontend weight alignment lost");
            std::vector<std::vector<float>> cepstra;
            for(auto& queue:normalized_queue) {
                auto frame=std::move(queue.front()); queue.pop_front();
                std::vector<float> input; input.reserve(22); input.push_back(frame.energy);
                input.insert(input.end(),frame.bands.begin(),frame.bands.end());
                cepstra.push_back(cepstrum.transform(input));
            }
            const float weight=weight_queue.front(); weight_queue.pop_front();
            selected(selector.push(cepstra,weight),output);
        }
    }
    void weighted(float weight,std::vector<SpeechFeatureFrame>& output) {
        if(filtered_queue.empty()) throw std::logic_error("Speech frontend filtered alignment lost");
        auto frame=std::move(filtered_queue.front()); filtered_queue.pop_front();
        weight_queue.push_back(weight);
        for(unsigned i=0;i<3;++i) {
            auto frames=normalizers[i].push(frame.branches[i].log_bands,frame.branches[i].log_noise,
                weight,frame.energy.energy,frame.energy.signal_level);
            for(auto& item:frames) normalized_queue[i].push_back(std::move(item));
        }
        normalized(output);
    }
    void active(bool activity,std::vector<SpeechFeatureFrame>& output) {
        if(band_queue.empty()) throw std::logic_error("Speech frontend band alignment lost");
        auto bands=std::move(band_queue.front()); band_queue.pop_front();
        activity_queue.push_back(activity);
        Filtered frame;
        float energy=0;
        for(unsigned i=0;i<3;++i) {
            frame.branches.push_back(noise_filters[i].push(bands[i],activity));
            energy+=frame.branches.back().log_energy;
        }
        frame.energy=filtered_energy.push(energy/3.0f);
        const auto weighting=adaptation.push(frame.energy.energy,frame.energy.activity);
        filtered_queue.push_back(std::move(frame));
        if(weighting) weighted(*weighting,output);
    }
    void windowed(std::span<const float> samples,std::vector<SpeechFeatureFrame>& output) {
        const auto frame=spectrum.transform(samples);
        std::vector<std::vector<float>> bands;
        for(const auto& bank:banks) bands.push_back(bank.transform(frame.power));
        band_queue.push_back(std::move(bands));
        const auto energy=raw_energy.push(frame.log_energy);
        if(auto activity=gate.push(energy.activity)) active(*activity,output);
    }
};
SpeechFrontend::SpeechFrontend(const OriginalSpeechData& language,SpeechNormalizationData normalization,
    std::uint32_t active_warps,bool relative_energy)
    : state_(std::make_unique<State>(language,std::move(normalization),active_warps,relative_energy)) {}
SpeechFrontend::~SpeechFrontend()=default;
std::vector<SpeechFeatureFrame> SpeechFrontend::push_block(std::span<const std::int16_t> pcm) {
    auto& state=*state_;
    if(state.finished) throw std::logic_error("Speech frontend already finished");
    std::vector<SpeechFeatureFrame> output;
    if(auto frame=state.window.push_block(pcm)) state.windowed(*frame,output);
    return output;
}
std::vector<SpeechFeatureFrame> SpeechFrontend::finish() {
    auto& state=*state_; std::vector<SpeechFeatureFrame> output;
    if(state.finished) return output;
    if(auto frame=state.window.finish()) state.windowed(*frame,output);
    for(bool activity:state.gate.finish()) state.active(activity,output);
    for(float weight:state.adaptation.finish()) state.weighted(weight,output);
    for(unsigned i=0;i<3;++i)
        for(auto& frame:state.normalizers[i].finish()) state.normalized_queue[i].push_back(std::move(frame));
    state.normalized(output);
    state.selected(state.selector.finish(),output);
    for(auto& frame:state.temporal.finish()) state.feature(std::move(frame),output);
    if(!state.band_queue.empty() || !state.filtered_queue.empty() || !state.weight_queue.empty() ||
       !state.activity_queue.empty()) throw std::logic_error("Speech frontend left unmatched frames");
    state.finished=true; return output;
}
unsigned SpeechFrontend::selected_branch()const { return state_->selector.selected_branch(); }
}
