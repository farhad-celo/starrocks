#pragma once

#include <boost/dynamic_bitset.hpp>

#include "column/hash_set.h"
#include "exprs/function_context.h"
#include "exprs/function_helper.h"
#include "util/phmap/phmap.h"

namespace starrocks {

struct PetriNet;

constexpr int64_t INCOMPLETE_VIOLATION_KEY = 2147483647;
constexpr int64_t TOO_COMPLEX_MODEL = 2147483646;
constexpr int64_t MISSING_START_ACTIVITY_KEY = 13370;

class CelonisConformance {
public:
    /**
     * @param: [activity array, json_petri_net_spec]
     * @paramType: [ARRAY_VARCHAR, VARCHAR]
     * @return: [ARRAY_BIGINT](conformance), [ARRAY_VARCHAR](readable_conformance)
     * Supports PQL IN https://docs.celonis.com/en/conformance.html
     *
     * json_petri_net_spec is a json version of PetriNetDescription message in
     * https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/protos/operators.proto
     * See be/test/exprs/celonis/conformance_test.cpp for an example.
     *
     * While the spec allows counts larger than 1 in initial_marking and final_marking, the implementation considers all
     * counts as 1 following Saola implementation.
     */
    DEFINE_VECTORIZED_FN(conformance);
    DEFINE_VECTORIZED_FN(readable_conformance);
    static Status conformance_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status conformance_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    template <bool readable>
    static ColumnPtr conformance_internal(const PetriNet& petri_net, const RunTimeColumnType<TYPE_VARCHAR>& elements,
                                          const UInt32Column& offsets, const NullColumn::Container* null_elements,
                                          const NullColumn::Container* null_arrays);
};

// The following implementation was copied from Saola implemention in
// https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/native/cpm-accelerator/modules/operators/process/conformance_operator.h
// and https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/native/cpm-accelerator/modules/operators/process/conformance_operator.cpp.
// The code was refactored as follows.
// - Made a PetriNet a pure data structure and added a separate build function. It can be constructed once per fragment.
// - Consolidated scattered Petri net search functions and states into PetriNetSearch.
// - Cleaned up unnecessary codes left from old changes and renamed functions and variables to match the current implementation.

// Describes a Petri net.
struct PetriNet {
    struct Transition;

    struct Place {
        Place(int index, std::string name) : index(index), name(std::move(name)) {}

        int index;
        std::string name;
        std::vector<Transition*> incoming_transitions;
        std::vector<Transition*> outgoing_transitions;
    };

    struct Transition {
        Transition(int index, std::string name, bool invisible)
                : index(index), name(std::move(name)), invisible(invisible) {}

        int index;
        std::string name;
        bool invisible;
        std::vector<Place*> incoming_places;
        std::vector<Place*> outgoing_places;
    };

    std::vector<Place> places;
    std::vector<Transition> transitions;
    // Mapped transitions per activity. key: activity row id
    phmap::flat_hash_map<int32_t, std::vector<Transition*>, StdHash<int32_t>> transition_mapping;
    // Initial states of the Petri net. Bits at indexes of places in initial marking are set.
    boost::dynamic_bitset<> initial_states;
    // Final states of the Petri net. Bits at indexes of places in final marking are set.
    boost::dynamic_bitset<> final_states;
};

// Helps to search a Petri net.
class PetriNetSearch {
public:
    explicit PetriNetSearch(const PetriNet& petri_net)
            : petri_net_(petri_net),
              cache_for_provide_(petri_net_.transitions.size()),
              cache_for_can_provide_(petri_net_.transitions.size()) {
        states_.push_back(petri_net_.initial_states);
    }

    // Tries to provide tokens for a mapped transition of an activity row by transferring tokens from incoming places to
    // outgoing places. Returns true if token are provided.
    bool try_provide_token(const PetriNet::Transition* transition, int row) {
        current_row_ = row;
        return try_provide_token(transition);
    }

    // Returns the current state.
    boost::dynamic_bitset<>& current_state() { return states_.back(); }

    // Pushes a new current state to the stack.
    void push_state(boost::dynamic_bitset<> state) { return states_.push_back(std::move(state)); }

    // Pops the current state from the stack.
    void pop_state() { states_.pop_back(); }

    // Returns true if the current state has reached an end place.
    bool is_complete_case_state();

    // Resets transition caches.
    void reset_transition_cache() {
        // Invalidates existing cache entries.
        cache_key_prefix_++;
    }

private:
    // Caches providing token results.
    class TransitionCache {
    public:
        explicit TransitionCache(int size) { cache_.resize(size); }

        struct CacheEntry {
            int cache_key_prefix;
            int row;
            bool value;
        };

        std::optional<bool> lookup(const PetriNet::Transition* transition, int cache_key_prefix, int row);

        void update(const PetriNet::Transition* transition, int cache_key_prefix, int row, bool value);

    private:
        std::vector<CacheEntry> cache_;
    };

    // Returns true if a token can be provided from a place.
    bool can_provide_token(const PetriNet::Place* place) const;

    // Tries to provide a token from a place. Returns true if a token will be provided. The caller is responsible for
    // transferring a token from the place to an outgoing place.
    bool try_provide_token(const PetriNet::Place* place);

    // Returns true if a transition is invisible and tokens can be provided from all incoming places.
    bool can_provide_token(const PetriNet::Transition* transition) const;

    // Tries to provide tokens for a transition by transferring tokens from incoming places to outgoing places. Returns
    // true if a token is provided.
    bool try_provide_token(const PetriNet::Transition* transition);

    // Returns true if a place has a token.
    bool has_token(const PetriNet::Place* place) const { return states_.back()[place->index]; }

    // Removes a token from a place.
    void unset_token(const PetriNet::Place* place) { states_.back()[place->index] = false; }

    // Adds a token to a place.
    void set_token(const PetriNet::Place* place) { states_.back()[place->index] = true; }

    const PetriNet& petri_net_;
    std::vector<boost::dynamic_bitset<>> states_;

    // For caches.
    int cache_key_prefix_{1};
    int current_row_;
    mutable TransitionCache cache_for_provide_;
    mutable TransitionCache cache_for_can_provide_;
};

class ConformanceCaseChecker {
public:
    ConformanceCaseChecker(int start_row, size_t size, const PetriNet& petri_net,
                           const RunTimeColumnType<TYPE_VARCHAR>& elements, const NullColumn::Container* null_elements);

    std::vector<RunTimeCppType<TYPE_BIGINT>> check();

private:
    int start_row_;
    size_t size_;
    const PetriNet& petri_net_;
    const RunTimeColumnType<TYPE_VARCHAR>& elements_;
    std::vector<int32_t> row_ids_;
    std::vector<const std::vector<PetriNet::Transition*>*> transitions_;
    std::vector<PetriNet::Transition*> empty_transitions_;
};

} // namespace starrocks