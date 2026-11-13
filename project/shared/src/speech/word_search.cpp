#include "speech/word_search.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace lifeline {
namespace {
constexpr auto unreachable = std::numeric_limits<std::int64_t>::max();
// Much longer than an interactive command; also bounds accumulated signed costs.
constexpr std::size_t maximum_frames = 1000000;
}

SpeechWordSearch::SpeechWordSearch(std::vector<SpeechWordPath> paths) {
    if (paths.empty() || paths.size() > 65536)
        throw std::invalid_argument("Invalid speech word path count");
    std::unordered_set<std::uint32_t> identifiers;
    std::size_t total_states = 0;
    for (auto& definition : paths) {
        if (definition.states.empty() || definition.states.size() > 65536 ||
            !identifiers.insert(definition.context_id).second)
            throw std::invalid_argument("Invalid or duplicate speech word path");
        total_states += definition.states.size();
        if (total_states > 1000000)
            throw std::invalid_argument("Speech word graph is too large");
        for (const auto state : definition.states)
            required_states_ = std::max(required_states_, std::size_t(state) + 1);
        auto costs = std::vector<std::int64_t>(definition.states.size(), unreachable);
        paths_.push_back({std::move(definition), std::move(costs)});
    }
}

std::vector<SpeechWordPath> SpeechWordSearch::command_paths(const OriginalSpeechData& data) {
    std::vector<SpeechWordPath> result;
    for (const auto& command : data.commands()) {
        if (command.prefix != 1)
            throw std::invalid_argument("Unsupported speech context path prefix");
        SpeechWordPath path{command.context_id, {}};
        for (const auto model : command.models) {
            if (model >= data.models().size())
                throw std::invalid_argument("Invalid speech model reference");
            const auto& states = data.models()[model].states;
            path.states.insert(path.states.end(), states.begin(), states.end());
        }
        result.push_back(std::move(path));
    }
    return result;
}

void SpeechWordSearch::reset() {
    frames_ = 0;
    for (auto& path : paths_)
        std::fill(path.costs.begin(), path.costs.end(), unreachable);
}

void SpeechWordSearch::push(std::span<const std::int16_t> state_costs) {
    if (state_costs.size() < required_states_)
        throw std::invalid_argument("Missing speech state costs (including derived states)");
    if (frames_ == maximum_frames)
        throw std::length_error("Speech utterance is too long");
    // 0x7fff denotes an uncomputed cost in the original cache, never evidence.
    // Validate before mutating any path so a rejected frame leaves state intact.
    for (const auto& path : paths_)
        for (const auto state : path.definition.states)
            if (state_costs[state] == 0x7fff)
                throw std::invalid_argument("Uncomputed speech acoustic state cost");

    for (auto& path : paths_) {
        // Descending order ensures both predecessors come from the previous frame.
        for (std::size_t i = path.costs.size(); i-- > 0;) {
            auto predecessor = path.costs[i];
            if (i != 0) predecessor = std::min(predecessor, path.costs[i - 1]);
            else if (frames_ == 0) predecessor = 0;
            path.costs[i] = predecessor == unreachable ? unreachable :
                predecessor + state_costs[path.definition.states[i]];
        }
    }
    ++frames_;
}

std::vector<SpeechWordCost> SpeechWordSearch::ranked() const {
    std::vector<SpeechWordCost> result;
    for (const auto& path : paths_)
        if (path.costs.back() != unreachable)
            result.push_back({path.definition.context_id, path.costs.back()});
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.cost < b.cost;
    });
    return result;
}
}
