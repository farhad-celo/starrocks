#include "exprs/celonis/match_process.h"

#include "column/array_column.h"
#include "column/column.h"
#include "exprs/celonis/util.h"
#include "rapidjson/document.h"

namespace starrocks {

static StatusOr<NFA::TransitionType> type_from_string(const std::string& input) {
    // Previously, "UNMATCHED" was implemented as e-transition. While it has been fixed, we keep old value until
    // migration is done.
    // TODO(j.kim): Move "UNMATCHED" to NFA::UNMATCHED once input migration is done.
    if (input == "UNMATCHED" || input == "E_TRANSITION") {
        return NFA::E_TRANSITION;
    } else if (input == "UNMATCHED2") {
        return NFA::UNMATCHED;
    } else if (input == "EXACT_MATCH") {
        return NFA::EXACT_MATCH;
    } else if (input == "LIKE") {
        return NFA::LIKE;
    } else if (input == "INVERSE_MATCH") {
        return NFA::INVERSE_MATCH;
    }
    return Status::JsonFormatError(Slice("Unsupported TransitionType in celonis_match_process"));
}

NFAEvaluator::NFAEvaluator(const NFA* nfa): nfa_(nfa) {
    std::vector<bool> state;
    state.resize(nfa->states.size());
    add_state_and_transitions(nfa->initial_state, &state);
    initial_dfa_state_ = to_dfa_state(state);
}

bool NFAEvaluator::matches(const std::vector<std::string>& activities) {
    int current_state = initial_dfa_state_;
    for (const std::string& activity : activities) {
        current_state = get_or_compute_updated_state(activity, current_state);
    }
    return final_dfa_states_[current_state];
}

int NFAEvaluator::to_dfa_state(const std::vector<bool>& nfa_state_set) {
    auto iter = nfa_states_to_dfa_state_.find(nfa_state_set);
    if (iter != nfa_states_to_dfa_state_.end()) {
        return iter->second;
    }

    int rv = nfa_states_to_dfa_state_.size();
    nfa_states_to_dfa_state_[nfa_state_set] = rv;
    dfa_state_to_nfa_states_[rv] = nfa_state_set;
    final_dfa_states_.resize(rv + 1);

    for (int i = 0; i < nfa_->states.size(); ++i) {
        if (nfa_->states[i]->is_final && nfa_state_set[i]) {
            final_dfa_states_[rv] = true;
        }
    }
    return rv;
}

int NFAEvaluator::get_or_compute_updated_state(const std::string& activity, int current_dfa_state) {
    auto rv_iter = dfa_transitions_.find({current_dfa_state, activity});
    if (rv_iter != dfa_transitions_.end()) {
        return rv_iter->second;
    }

    int rv = to_dfa_state(compute_updated_state(activity, dfa_state_to_nfa_states_[current_dfa_state]));
    dfa_transitions_[TransitionKey{current_dfa_state, activity}] = rv;

    return rv;
}

std::vector<bool>
NFAEvaluator::compute_updated_state(const std::string& activity, const std::vector<bool>& current_state) {
    std::vector<bool> new_state(current_state.size());
    for (int i = 0; i < nfa_->states.size(); ++i) {
        if (!current_state[i]) {
            continue;
        }
        const NFA::State& state = *nfa_->states[i];
        auto maybe_fire_transitions = [&](bool no_matches) -> bool {
            bool any_matches = false;
            for (const auto& transition : state.transitions) {
                if ((no_matches && transition->type == NFA::UNMATCHED) ||
                    transition_matches_activity(activity, *transition)) {
                    any_matches = true;
                    for (int out : transition->to_states) {
                        add_state_and_transitions(out, &new_state);
                    }
                }
            }
            return any_matches;
        };
        if (!maybe_fire_transitions(false)) {
            maybe_fire_transitions(true);
        }
    }
    return new_state;
}

void NFAEvaluator::add_state_and_transitions(int new_state, std::vector<bool>* current_state) {
    if ((*current_state)[new_state]) {
        return;
    }

    (*current_state)[new_state] = true;
    std::vector<int> to_visit = {new_state};

    while (!to_visit.empty()) {
        auto back = to_visit.back();
        to_visit.pop_back();
        for (const auto& transition : nfa_->states[back]->transitions) {
            if (transition->type == NFA::E_TRANSITION) {
                for (int out :  transition->to_states) {
                    if (!(*current_state)[out]) {
                        (*current_state)[out] = true;
                        to_visit.push_back(out);
                    }
                }
            }
        }
    }
}

StatusOr<std::string> convert_like_pattern(const std::string& pattern) {
    std::string re_pattern;
    re_pattern.clear();

    bool is_escaped = false;

    re_pattern.append("^");
    bool has_unescaped_wildcards = false;

    for (int i = 0; i < pattern.size(); ++i) {
        if (!is_escaped && pattern[i] == '%') {
            re_pattern.append(".*");
            has_unescaped_wildcards = true;
        } else if (!is_escaped && pattern[i] == '_') {
            re_pattern.append(".");
            has_unescaped_wildcards = true;
            // check for escape char before checking for regex special chars, they might overlap
        } else if (!is_escaped && pattern[i] == '\\') {
            is_escaped = true;
        } else if (pattern[i] == '.' || pattern[i] == '[' || pattern[i] == ']' ||
                   pattern[i] == '{' || pattern[i] == '}' || pattern[i] == '(' ||
                   pattern[i] == ')' || pattern[i] == '\\' || pattern[i] == '*' ||
                   pattern[i] == '+' || pattern[i] == '?' || pattern[i] == '|' ||
                   pattern[i] == '^' || pattern[i] == '$') {
            re_pattern.append("\\");
            re_pattern.append(1, pattern[i]);
            is_escaped = false;
        } else {
            // regular character or escaped special character
            re_pattern.append(1, pattern[i]);
            is_escaped = false;
        }
    }

    if (!has_unescaped_wildcards) {
        return Status::RuntimeError(fmt::format("Regex {} should have wildcards", pattern));
    }

    re_pattern.append("$");

    return re_pattern;
}


bool NFAEvaluator::transition_matches_activity(const std::string& activity, const NFA::Transition& transition) {
    switch (transition.type) {
        case NFA::E_TRANSITION:
            return false;
        case NFA::UNMATCHED:
            return false;
        case NFA::EXACT_MATCH:
            return transition.activity_names.count(activity);
        case NFA::INVERSE_MATCH:
            return !transition.activity_names.count(activity);
        case NFA::LIKE:
            if (transition.activity_names.count("%")) {
                return true;
            }
            return transition.regex_patterns->Match(re2::StringPiece(activity.c_str(), activity.size()), nullptr);
    }
    std::stringstream error;
    error << "Unhandled case: " << transition.type << std::endl;
    throw std::runtime_error(error.str());
}


static StatusOr<std::unique_ptr<NFA>> from_json(const std::string& input) {
    rapidjson::Document document;
    document.Parse(input.c_str());
    std::unique_ptr<NFA> result = std::make_unique<NFA>();

    result->initial_state = document["initialState"].GetInt();

    const rapidjson::Value& states = document["states"];
    for (rapidjson::SizeType i = 0; i < states.Size(); ++i) {
        std::unique_ptr<NFA::State> state = std::make_unique<NFA::State>();
        const rapidjson::Value& transitions = states[i]["transitions"];
        state->is_final = states[i]["final"].GetBool();
        for (rapidjson::SizeType j = 0; j < transitions.Size(); ++j) {
            std::unique_ptr<NFA::Transition> transition = std::make_unique<NFA::Transition>();
            ASSIGN_OR_RETURN(transition->type, type_from_string(transitions[j]["type"].GetString()));
            const rapidjson::Value& to_states = transitions[j]["toStates"];
            for (rapidjson::SizeType k = 0; k < to_states.Size(); ++k) {
                transition->to_states.push_back(to_states[k].GetInt());
            }
            const rapidjson::Value& activity_names = transitions[j]["activityNames"];
            for (rapidjson::SizeType k = 0; k < activity_names.Size(); ++k) {
                transition->activity_names.insert(activity_names[k].GetString());
            }
            if (transition->type == NFA::LIKE) {
                RE2::Options opts;
                opts.set_never_nl(false);
                opts.set_dot_nl(true);
                opts.set_log_errors(false);
                std::unique_ptr<re2::RE2::Set> regex_set = std::make_unique<re2::RE2::Set>(opts, RE2::UNANCHORED);
                for (const std::string& activity : transition->activity_names) {
                    ASSIGN_OR_RETURN(const std::string regex, convert_like_pattern(activity));
                    std::string err;
                    if (regex_set->Add(regex, &err) < 0) {
                        return Status::RuntimeError(fmt::format("Error adding to regex {}, expression {}", err, regex));
                    }
                }
                if (!regex_set->Compile()) {
                    return Status::RuntimeError("Error compiling regex");
                }
                transition->regex_patterns = std::move(regex_set);
            }
            state->transitions.push_back(std::move(transition));
        }
        result->states.push_back(std::move(state));
    }
    return result;
}

ColumnPtr celonis_match_process_internal(const NFA* nfa, const Column& elements,
                                         const UInt32Column& offsets,
                                         const NullColumn::Container* null_element_offsets,
                                         const NullColumn::Container* null_array_offsets) {
    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(num_array);
    result.reserve(num_array);
    using ValueType = RunTimeCppType<TYPE_VARCHAR>;
    auto elements_ptr = (const ValueType *) (elements.raw_data());
    std::vector<std::string> current_array;

    NFAEvaluator nfa_eval(nfa);

    for (size_t i = 0; i < num_array; i++) {
        if (null_array_offsets != nullptr && (*null_array_offsets)[i]) {
            result.append_null();
            continue;
        }
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        current_array.clear();
        for (size_t index = 0; index < array_size; ++index) {
            if (null_element_offsets != nullptr && (*null_element_offsets)[offset + index] != 0) {
                continue;
            }

            const auto &value = elements_ptr[offset + index];
            current_array.push_back(value.to_string());
        }
        if (nfa_eval.matches(current_array)) {
            result.append(1L);
        } else {
            result.append(0L);
        }
    }
    return result.build(/*is_const=*/false);
}

// Keeps the NFA that defines matching. Initialized once per thread.
struct MatchProcessState {
    std::unique_ptr<NFA> nfa;
};

Status CelonisMatchProcess::match_process_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    if (context->get_num_constant_columns() != 2) {
        return Status::InvalidArgument(
                "celonis_match_process needs 2 parameters: column, json spec");
    }
    if (!context->is_notnull_constant_column(1)) {
        return Status::OK();
    }
    const auto json_input = context->get_constant_column(1);
    std::string json = ColumnHelper::get_const_value<TYPE_VARCHAR>(json_input).to_string();
    StatusOr<std::unique_ptr<NFA>> nfa_status = from_json(json);
    if (!nfa_status.ok()) {
        std::stringstream error;
        error << "can't parse JSON specification in celonis_match_process" << std::endl;
        throw std::runtime_error(error.str());
    }
    auto *state = new MatchProcessState();
    state->nfa = std::move(nfa_status.value());
    context->set_function_state(scope, state);

    return Status::OK();
}

Status CelonisMatchProcess::match_process_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<MatchProcessState*>(context->get_function_state(scope));
        delete state;
    }

    return Status::OK();
}

StatusOr<ColumnPtr> CelonisMatchProcess::celonis_match_process(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const auto* state = reinterpret_cast<const MatchProcessState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    DCHECK(state != nullptr);
    const auto& nfa = state->nfa;
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    return celonis_match_process_internal(nfa.get(), *array_data.elements, *array_data.offsets, array_data.null_elements, array_data.null_arrays);
}
} // namespace starrocks
