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
#include "util/hash.h"
#include "util/phmap/phmap.h"

namespace starrocks {

struct VectorBoolHash {
    std::size_t operator()(const std::vector<bool>& vec) const {
        std::size_t hash = 0;
        for (bool b : vec) {
            hash ^= std::hash<bool>{}(b) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

struct NFA;

class CelonisMatchProcess {
public:
    /**
     * @param: [activity_list, nfa_json_spec]
     * @paramType: [ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Implements PQL MATCH_PROCESS: https://docs.celonis.com/en/match_process.html
     */
    DEFINE_VECTORIZED_FN(celonis_match_process);

    static Status match_process_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status match_process_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    // Instead of preparing the NFA from context's json column, prepare it from @param nfa. Only used in the benchmarks.
    static Status match_process_prepare_benchmark_only(FunctionContext* context, std::unique_ptr<NFA> nfa,
                                                       FunctionContext::FunctionStateScope scope);
};

// Describes the NFA structure.
struct NFA {
    enum TransitionType { E_TRANSITION = 0, UNMATCHED = 1, EXACT_MATCH = 2, LIKE = 3, INVERSE_MATCH = 4 };

    struct Transition {
        TransitionType type;
        std::vector<int> to_states;
        SliceHashSet activity_names;
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

    bool matches(const std::vector<Slice>& activities);

private:
    const NFA* nfa_;
    int initial_dfa_state_;

    struct TransitionKey {
        int current_dfa_state;
        SliceWithHash input_symbol;

        TransitionKey(int current_state, const Slice& input_symbol)
                : current_dfa_state(current_state), input_symbol(input_symbol){};

        bool operator==(const TransitionKey& other) const {
            return (current_dfa_state == other.current_dfa_state && input_symbol == other.input_symbol);
        }
    };

    struct TransitionKeyHasher {
        std::size_t operator()(const TransitionKey& k) const {
            return ((std::hash<int>()(k.current_dfa_state) ^ (k.input_symbol.hash << 1)) >> 1);
        }
    };

    // Based on the benchmark, phmap::flat_hash_map has better performance (up to 36% latency reduction)
    // than std::unordered_map when the number nfa states is small.
    phmap::flat_hash_map<TransitionKey, int, TransitionKeyHasher> dfa_transitions_;

    phmap::flat_hash_map<std::vector<bool>, int, VectorBoolHash> nfa_states_to_dfa_state_;

    phmap::flat_hash_map<int, std::vector<bool>, StdHash<int>> dfa_state_to_nfa_states_;

    // Based on the benchmark, using BitVector gives similar performance.
    std::vector<bool> final_dfa_states_;

    int to_dfa_state(const std::vector<bool>& nfa_state_set);

    void add_state_and_transitions(int new_state, std::vector<bool>* state);

    std::vector<bool> compute_updated_state(const Slice& activity, const std::vector<bool>& current_state);

    bool transition_matches_activity(const Slice& activity, const NFA::Transition& transition);

    int get_or_compute_updated_state(const Slice& activity, int current_dfa_state);
};

} // namespace starrocks
