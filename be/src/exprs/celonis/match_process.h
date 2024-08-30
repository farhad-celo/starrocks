#pragma once

#include <re2/set.h>
#include <string>
#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"
#include "column/hash_set.h"

namespace starrocks {

class CelonisMatchProcess {
public:
    DEFINE_VECTORIZED_FN(celonis_match_process);
    static Status match_process_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status match_process_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

// Describes the NFA structure.
struct NFA {
    enum TransitionType {
        E_TRANSITION = 0, UNMATCHED = 1, EXACT_MATCH = 2, LIKE = 3, INVERSE_MATCH = 4
    };

    struct Transition {
        TransitionType type;
        std::vector<int> to_states;
        HashSet<std::string> activity_names;
        // for transition of type LIKE, keep the precompiled regular expressions for matching.
        std::unique_ptr<RE2::Set> regex_patterns;
    };

    struct State {
        std::vector<std::unique_ptr<Transition>> transitions;
        bool is_final;
    };

    std::vector<std::unique_ptr<State>> states;
    int initial_state = 0;
};

// Evaluates the NFA over the given array of strings.
class NFAEvaluator {
public:
    NFAEvaluator(const NFA* nfa);
    bool matches(const std::vector<std::string>& activities);

private:
    const NFA* nfa_;
    int initial_dfa_state_;

    struct TransitionKey {
        int current_dfa_state;
        std::string input_symbol;

        TransitionKey(int current_state, const std::string& input_symbol) : current_dfa_state(current_state),
                                                                            input_symbol(input_symbol) {};

        bool operator==(const TransitionKey& other) const {
            return (current_dfa_state == other.current_dfa_state
                    && input_symbol == other.input_symbol);
        }
    };

    struct TransitionKeyHasher {
        std::size_t operator()(const TransitionKey& k) const {
            return ((std::hash<int>()(k.current_dfa_state)
                     ^ (std::hash<std::string>()(k.input_symbol) << 1)) >> 1);
        }
    };

    std::unordered_map<TransitionKey, int, TransitionKeyHasher> dfa_transitions_;

    std::unordered_map<std::vector<bool>, int> nfa_states_to_dfa_state_;

    std::unordered_map<int, std::vector<bool>> dfa_state_to_nfa_states_;

    std::vector<bool> final_dfa_states_;

    int to_dfa_state(const std::vector<bool>& nfa_state_set);

    void add_state_and_transitions(int new_state, std::vector<bool>* state);

    std::vector<bool> compute_updated_state(const std::string& activity, const std::vector<bool>& current_state);

    bool transition_matches_activity(const std::string& activity, const NFA::Transition& transition);

    int get_or_compute_updated_state(const std::string& activity, int current_dfa_state);
};

} // namespace starrocks
