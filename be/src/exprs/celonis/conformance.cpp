#include "exprs/celonis/conformance.h"

#include <boost/functional/hash.hpp>
#include <limits>
#include <memory>
#include <stack>
#include <unordered_set>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "exprs/celonis/id.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"
#include "rapidjson/document.h"

namespace starrocks {

// Cache entry for checking visited states.
class PetriNetStateCacheEntry {
public:
    PetriNetStateCacheEntry(boost::dynamic_bitset<> bit_set, int log_position, int current_violations)
            : bit_set(std::move(bit_set)), log_position(log_position), current_violations(current_violations) {}
    boost::dynamic_bitset<> bit_set;
    int log_position;
    int current_violations;
    PetriNetStateCacheEntry(const PetriNetStateCacheEntry& c) = default;
    PetriNetStateCacheEntry(PetriNetStateCacheEntry&& c) = default;
};

} // namespace starrocks

namespace std {

template <>
struct equal_to<starrocks::PetriNetStateCacheEntry> {
    bool operator()(const starrocks::PetriNetStateCacheEntry& left,
                    const starrocks::PetriNetStateCacheEntry& right) const {
        return left.log_position == right.log_position && left.current_violations == right.current_violations &&
               left.bit_set == right.bit_set;
    }
};

template <>
struct hash<starrocks::PetriNetStateCacheEntry> {
    size_t operator()(const starrocks::PetriNetStateCacheEntry& c) const {
        size_t res = boost::hash_value(c.bit_set);
        boost::hash_combine(res, c.log_position);
        boost::hash_combine(res, c.current_violations);
        return res;
    }
};

} // namespace std

namespace starrocks {

namespace petri_net_builder {

std::optional<std::string> get_duplicated_activities_string(const rapidjson::Value& mapping) {
    std::map<std::string, int> freq_map;

    for (rapidjson::SizeType i = 0; i < mapping.Size(); ++i) {
        const rapidjson::Value& activity = mapping[i]["from"];
        freq_map[activity.GetString()]++;
    }

    std::string duplicates;
    for (const auto& curr_pair : freq_map) {
        if (curr_pair.second > 1) {
            duplicates.append(fmt::format("[{}] occurs {} times, ", curr_pair.first, curr_pair.second));
        }
    }

    if (duplicates.empty()) {
        return std::nullopt;
    }

    // Removes the unnecessary ", " at the end.
    duplicates.pop_back();
    duplicates.pop_back();
    return duplicates;
}

Status add_arc(const std::string& from, const std::string& to, std::map<std::string, PetriNet::Place*>& place_index,
               std::map<std::string, PetriNet::Transition*>& transition_index) {
    if (place_index.find(from) == place_index.end()) {
        if (place_index.find(to) == place_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Place [" << from << "] and Place [" << to << "] not found.";
            return Status::InvalidArgument(error.str());
        }
        if (transition_index.find(from) == transition_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Transition [" << from << "] not found.";
            return Status::InvalidArgument(error.str());
        }
        transition_index[from]->outgoing_places.push_back(place_index[to]);
        place_index[to]->incoming_transitions.push_back(transition_index[from]);
    } else {
        if (transition_index.find(to) == transition_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Transition [" << to << "] not found.";
            return Status::InvalidArgument(error.str());
        }
        place_index[from]->outgoing_transitions.push_back(transition_index[to]);
        transition_index[to]->incoming_places.push_back(place_index[from]);
    }
    return Status::OK();
}

StatusOr<std::unique_ptr<PetriNet>> build(const std::string& json_petri_net_spec, FunctionContext* context) {
    auto petri_net = std::make_unique<PetriNet>();

    rapidjson::Document document;
    document.Parse(json_petri_net_spec.c_str());
    if (document.HasParseError()) {
        std::stringstream error;
        error << "celonis_conformance: Can't parse JSON specification.";
        return Status::InvalidArgument(error.str());
    }

    if (!document.HasMember("mapping")) {
        std::stringstream error;
        error << "celonis_conformance: Your model does not contain 'mapping'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& mapping_values = document["mapping"];
    const auto duplicates_warning{get_duplicated_activities_string(mapping_values)};
    if (duplicates_warning.has_value()) {
        constexpr std::string_view WARNING_MSG{
                "celonis_conformance: Your model contains duplicate activities: {}. Please try to create a model "
                "without duplicate activity names as they may cause severe performance issues."};
        context->add_warning(fmt::format(WARNING_MSG, duplicates_warning.value()).c_str());
    }

    if (!document.HasMember("places")) {
        std::stringstream error;
        error << "celonis_conformance: Your model does not contain 'places'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& place_values = document["places"];
    for (rapidjson::SizeType i = 0; i < place_values.Size(); ++i) {
        petri_net->places.emplace_back(i, place_values[i].GetString());
    }
    petri_net->initial_states = boost::dynamic_bitset<>(petri_net->places.size());
    petri_net->final_states = boost::dynamic_bitset<>(petri_net->places.size());

    if (!document.HasMember("transitions")) {
        std::stringstream error;
        error << "celonis_conformance: Your model does not contain 'transitions'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& transition_values = document["transitions"];

    for (rapidjson::SizeType i = 0; i < transition_values.Size(); ++i) {
        petri_net->transitions.emplace_back(i, transition_values[i].GetString(), true);
    }

    std::map<std::string, PetriNet::Place*> place_index;
    std::map<std::string, PetriNet::Transition*> transition_index;
    for (auto& place : petri_net->places) {
        place_index[place.name] = &place;
    }
    for (auto& transition : petri_net->transitions) {
        transition_index[transition.name] = &transition;
    }

    if (!document.HasMember("initial_marking")) {
        std::stringstream error;
        error << "celonis_conformance: Your model does not contain 'initial_marking'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& initial_marking_values = document["initial_marking"];
    // Saola implemtation ignores count.
    for (rapidjson::SizeType i = 0; i < initial_marking_values.Size(); ++i) {
        const auto& start_place = initial_marking_values[i]["node"].GetString();
        if (place_index.find(start_place) == place_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Initial marking [" << start_place << "] not found in 'places'.";
            return Status::InvalidArgument(error.str());
        }
        petri_net->initial_states[place_index[start_place]->index] = true;
    }

    if (!document.HasMember("final_marking")) {
        std::stringstream error;
        error << "celonis_conformance: Your model does not contain 'final_marking'.";
        return Status::InvalidArgument(error.str());
    }
    const rapidjson::Value& final_marking_values = document["final_marking"];
    for (rapidjson::SizeType i = 0; i < final_marking_values.Size(); ++i) {
        const auto& end_place = final_marking_values[i]["node"].GetString();
        if (place_index.find(end_place) == place_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Final marking [" << end_place << "] not found in 'places'.";
            return Status::InvalidArgument(error.str());
        }
        petri_net->final_states[place_index[end_place]->index] = true;
    }

    const rapidjson::Value& arc_values = document["arcs"];
    for (rapidjson::SizeType i = 0; i < arc_values.Size(); ++i) {
        RETURN_IF_ERROR(add_arc(arc_values[i]["from"].GetString(), arc_values[i]["to"].GetString(), place_index,
                                transition_index));
    }

    for (rapidjson::SizeType i = 0; i < mapping_values.Size(); ++i) {
        const std::string& activity_name = mapping_values[i]["from"].GetString();
        const std::string& transition_name = mapping_values[i]["to"].GetString();

        int32_t rid = Id::get(Slice(activity_name));
        if (transition_index.find(transition_name) == transition_index.end()) {
            std::stringstream error;
            error << "celonis_conformance: Transition [" << transition_name << "] not found while mapping activity ["
                  << activity_name << "].";
            return Status::InvalidArgument(error.str());
        }
        auto* transition = transition_index[transition_name];
        transition->invisible = false;
        petri_net->transition_mapping[rid].push_back(transition);
    }
    return petri_net;
}

} // namespace petri_net_builder

struct ConformanceState {
    std::unique_ptr<PetriNet> petri_net;
};

std::string conformance_result_to_string(int64_t result, const RunTimeColumnType<TYPE_VARCHAR>& elements, int index,
                                         int last_conform_index) {
    // To directly decode a result to a string, we need to map activity ids in the result to activity strings. For this,
    // we would need to maintain a hash map. But actually it is not necessary to map activity ids at all. Instead, we
    // ignore activity ids in the result and deduce activity names from the current index and by tracking the last
    // conforming activity.
    if (result == 0) {
        return "Conforms";
    } else if (result == INCOMPLETE_VIOLATION_KEY) {
        return "Incomplete";
    } else if (result == TOO_COMPLEX_MODEL) {
        return "Too complex";
    } else if (result < 0) {
        return elements.get_slice(index).to_string() + " is an undesired activity";
    }
    int32_t last_activity = result >> 32;
    if (last_activity == MISSING_START_ACTIVITY_KEY) {
        return elements.get_slice(index).to_string() + " is executed as start activity";
    } else if (last_conform_index >= 0) {
        return elements.get_slice(last_conform_index).to_string() + " is followed by " +
               elements.get_slice(index).to_string();
    }
    return "Unknown violation";
}

template <bool readable>
ColumnPtr CelonisConformance::conformance_internal(const PetriNet& petri_net,
                                                   const RunTimeColumnType<TYPE_VARCHAR>& elements,
                                                   const UInt32Column& offsets,
                                                   const NullColumn::Container* null_elements,
                                                   const NullColumn::Container* null_arrays) {
    using ColumnType = std::conditional_t<readable, RunTimeColumnType<TYPE_VARCHAR>, RunTimeColumnType<TYPE_BIGINT>>;

    const size_t num_array = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(ColumnType::create(), NullColumn::create()),
                                            UInt32Column::create());
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    ColumnPtr& result_elements = result_array->elements_column();
    result_offsets.reserve(num_array);
    result_elements->reserve(elements.size());
    size_t new_offset = 0;

    std::vector<std::string> current_array;

    for (size_t i = 0; i < num_array; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((null_arrays != nullptr && (*null_arrays)[i]) || array_size == 0) {
            // If null_array_offset[i] is true, the current array is NULL.
            // TODO(j.kim): Fix the inconsistency of NULL behavior. Returning an empty array here vs Null for Null literal.
            // If array_size is 0, the current array is empty.
            result_offsets.push_back(new_offset);
            continue;
        }

        ConformanceCaseChecker checker(offset, array_size, petri_net, elements, null_elements);
        auto results = checker.check();
        int last_conform_index = -1;
        for (int j = 0; j < results.size(); j++) {
            auto result = results[j];
            if constexpr (readable) {
                if (result == 0 && (null_elements == nullptr || (*null_elements)[offset + j] == 0)) {
                    last_conform_index = offset + j;
                }
                result_elements->append_datum(Slice(conformance_result_to_string(result, elements, offset + j,
                                                                                 last_conform_index)));
            } else {
                result_elements->append_datum(result);
            }
        }
        new_offset += results.size();
        result_offsets.push_back(new_offset);
    }
    return result_array;
}

Status CelonisConformance::conformance_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    if (context->get_num_constant_columns() != 2) {
        return Status::InvalidArgument("celonis_conformance needs 2 parameters: column, json spec");
    }
    if (!context->is_notnull_constant_column(1)) {
        return Status::InvalidArgument("celonis_conformance only supports constant json spec");
    }

    const auto json_input = context->get_constant_column(1);
    std::string json = ColumnHelper::get_const_value<TYPE_VARCHAR>(json_input).to_string();

    auto* state = new ConformanceState();
    context->set_function_state(scope, state);
    ASSIGN_OR_RETURN(state->petri_net, petri_net_builder::build(json, context));

    return Status::OK();
}

Status CelonisConformance::conformance_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* state = reinterpret_cast<ConformanceState*>(context->get_function_state(scope));
        delete state;
    }

    return Status::OK();
}

StatusOr<ColumnPtr> CelonisConformance::conformance(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    const auto* state =
            reinterpret_cast<const ConformanceState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    return conformance_internal</*readable=*/false>(*state->petri_net,
            *down_cast<const RunTimeColumnType<TYPE_VARCHAR>*>(array_data.elements),
            *array_data.offsets, array_data.null_elements, array_data.null_arrays);
}

StatusOr<ColumnPtr> CelonisConformance::readable_conformance(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    const auto* state =
            reinterpret_cast<const ConformanceState*>(context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    return conformance_internal</*readable=*/true>(*state->petri_net,
            *down_cast<const RunTimeColumnType<TYPE_VARCHAR>*>(array_data.elements),
            *array_data.offsets, array_data.null_elements, array_data.null_arrays);
}

std::optional<bool> PetriNetSearch::TransitionCache::lookup(const PetriNet::Transition* transition,
                                                            int cache_key_prefix, int row) {
    const auto& entry = cache_[transition->index];
    if (entry.cache_key_prefix == cache_key_prefix && entry.row == row) {
        return entry.value;
    }
    return std::nullopt;
}

void PetriNetSearch::TransitionCache::update(const PetriNet::Transition* transition, int cache_key_prefix, int row,
                                             bool value) {
    cache_[transition->index] = {cache_key_prefix, row, value};
}

bool PetriNetSearch::is_complete_case_state() {
    // If a final place can provide a token, the case is complete.
    reset_transition_cache();
    for (size_t index = petri_net_.final_states.find_first(); index != petri_net_.final_states.npos;
         index = petri_net_.final_states.find_next(index)) {
        auto final_place = &petri_net_.places[index];
        if (can_provide_token(final_place)) {
            return true;
        }
    }
    return false;
}

bool PetriNetSearch::can_provide_token(const PetriNet::Place* place) const {
    if (has_token(place)) {
        return true;
    }

    // If a token can be provided from an invisible incoming transition, it can be provided by the place.
    for (const auto& incoming_transition : place->incoming_transitions) {
        if (can_provide_token(incoming_transition)) {
            return true;
        }
    }
    return false;
}

bool PetriNetSearch::try_provide_token(const PetriNet::Place* place) {
    if (has_token(place)) {
        // Transferring the token will be done by a transition.
        return true;
    }

    // Trys to provide a token through incoming transitions.
    for (const auto& incoming_transition : place->incoming_transitions) {
        if (can_provide_token(incoming_transition)) {
            return try_provide_token(incoming_transition);
        }
    }
    return false;
}

bool PetriNetSearch::can_provide_token(const PetriNet::Transition* transition) const {
    if (!transition->invisible) {
        return false;
    }
    auto result = cache_for_can_provide_.lookup(transition, cache_key_prefix_, current_row_);
    if (result.has_value()) {
        return result.value();
    }
    for (const auto& incoming_place : transition->incoming_places) {
        // Only when all places can provide tokens, a token can be provided from the invisible transition.
        if (!can_provide_token(incoming_place)) {
            cache_for_can_provide_.update(transition, cache_key_prefix_, current_row_, false);
            return false;
        }
    }
    cache_for_can_provide_.update(transition, cache_key_prefix_, current_row_, true);
    return true;
}

bool PetriNetSearch::try_provide_token(const PetriNet::Transition* transition) {
    bool ret = true;
    auto tried_before = cache_for_provide_.lookup(transition, cache_key_prefix_, current_row_);
    if (tried_before.has_value()) {
        ret = tried_before.value();
    } else {
        for (const auto& incoming_place : transition->incoming_places) {
            if (!try_provide_token(incoming_place)) {
                ret = false;
            }
            // Transfers a token.
            unset_token(incoming_place);
        }
        cache_for_provide_.update(transition, cache_key_prefix_, current_row_, ret);
    }
    for (const auto& outgoing_place : transition->outgoing_places) {
        set_token(outgoing_place);
    }
    return ret;
}

ConformanceCaseChecker::ConformanceCaseChecker(int start_row, size_t size, const PetriNet& petri_net,
                                               const RunTimeColumnType<TYPE_VARCHAR>& elements,
                                               const NullColumn::Container* null_elements)
        : start_row_(start_row), size_(size), petri_net_(petri_net), elements_(elements) {
    row_ids_.reserve(size);
    transitions_.reserve(size);
    for (int i = 0; i < size; i++) {
        int32_t rid = 0;
        if (null_elements == nullptr || (*null_elements)[start_row + i] == 0) {
            rid = Id::get(elements_.get_slice(start_row_ + i));
        }
        row_ids_.push_back(rid);
        auto it = petri_net_.transition_mapping.find(rid);
        if (it == petri_net_.transition_mapping.end()) {
            transitions_.push_back(&empty_transitions_);
        } else {
            transitions_.push_back(&it->second);
        }
    }
}

namespace {
struct StackItem {
    using bitset_t = boost::dynamic_bitset<>;
    explicit StackItem(bool push_bitset, int number_of_violations, int index, bitset_t&& bitset_to_push = bitset_t())
            : number_of_violations(number_of_violations),
              index(index),
              push_bitset(push_bitset),
              pop_bitset(false),
              bitset_to_push(bitset_to_push) {}
    static StackItem marker_pop_net_state() { return StackItem(true); }
    int number_of_violations{0};
    int index{0};
    bool push_bitset{false};
    bool pop_bitset;
    bitset_t bitset_to_push;

private:
    explicit StackItem(bool pop_bitset)
            : pop_bitset(pop_bitset) {} // Constructs StackItem to mark pop of net.states stack.
};
} // namespace

std::vector<RunTimeCppType<TYPE_BIGINT>> ConformanceCaseChecker::check() {
    std::vector<RunTimeCppType<TYPE_BIGINT>> temporary_result(size_);
    std::vector<RunTimeCppType<TYPE_BIGINT>> result(size_);

    PetriNetSearch petri_net_search(petri_net_);
    int32_t last_read_activity{MISSING_START_ACTIVITY_KEY};
    int minimum_violations{INT_MAX};
    std::unordered_set<PetriNetStateCacheEntry> visited_states;

    std::stack<StackItem> stack;
    stack.emplace(false, 0, 0);

    while (!stack.empty()) {
        StackItem stacktop = stack.top();
        stack.pop();
        if (stacktop.pop_bitset) {
            petri_net_search.pop_state();
            continue;
        }
        if (stacktop.push_bitset) {
            petri_net_search.push_state(std::move(stacktop.bitset_to_push));
            // Schedules a removal of the state.
            stack.push(StackItem::marker_pop_net_state());
        }
        int number_of_violations = stacktop.number_of_violations;
        int index = stacktop.index;

        // Drops the current branch if it is worse than the best result we have so far.
        if (number_of_violations >= minimum_violations) {
            continue;
        }

        // Checks if we have reached the end of a case.
        if (index >= size_) {
            // Checks if it is really the end of the case or if it is incomplete.
            bool incomplete_case = false;
            if (number_of_violations == 0) {
                if (!petri_net_search.is_complete_case_state()) {
                    incomplete_case = true;
                    temporary_result[index - 1] = INCOMPLETE_VIOLATION_KEY;
                    number_of_violations++;
                }
            }

            // Updates the result if the new result is better.
            if (number_of_violations < minimum_violations || incomplete_case) {
                minimum_violations = number_of_violations;
                result = temporary_result;
            }
            continue;
        }

        int32_t activity_rid = row_ids_[index];

        if (activity_rid == 0) {
            // Result: Conforming. A NULL value conforms with any Petri net.
            temporary_result[index] = 0;
            stack.emplace(false, number_of_violations, index + 1);
            continue;
        }

        auto& transitions_for_activity = *transitions_[index];
        auto transition_count = transitions_for_activity.size();
        if (transition_count == 0) {
            // Result: Not conforming. Missing activity
            temporary_result[index] = -1 * static_cast<int64_t>(activity_rid);
            stack.emplace(false, number_of_violations + 1, index + 1);
        } else if (transition_count == 1) {
            // Resets cache because we might have stepped back a row id.
            petri_net_search.reset_transition_cache();
            if (petri_net_search.try_provide_token(transitions_for_activity[0], index)) {
                // Result: Conforming
                temporary_result[index] = 0;
                last_read_activity = activity_rid;
                stack.emplace(false, number_of_violations, index + 1);
            } else {
                // Result: Not conforming
                temporary_result[index] = (static_cast<int64_t>(last_read_activity) << 32) + activity_rid;
                last_read_activity = activity_rid;
                stack.emplace(false, number_of_violations + 1, index + 1);
            }
        } else {
            // There are multiple mapping transitions.
            // Checks all transitions first. Then explores all valid transitions through the stack.
            // Step 1. Checks all transitions.
            bool invalid_state_found = false;
            boost::dynamic_bitset<> invalid_state;
            int64_t invalid_state_result = 0L;
            std::vector<boost::dynamic_bitset<>> valid_states;
            for (auto transition : transitions_for_activity) {
                petri_net_search.push_state(petri_net_search.current_state());
                // Resets cache because we might have stepped back a row id
                petri_net_search.reset_transition_cache();
                bool token_provided = petri_net_search.try_provide_token(transition, index);
                // Skips the state if it has been examined before.
                PetriNetStateCacheEntry cache_entry(petri_net_search.current_state(), index,
                                                    (token_provided ? number_of_violations : number_of_violations + 1));
                if (visited_states.find(cache_entry) != visited_states.end()) {
                    petri_net_search.pop_state();
                    continue;
                }

                visited_states.insert(std::move(cache_entry));
                if (visited_states.size() > (2 << 20)) {
                    std::fill(result.begin(), result.end(), TOO_COMPLEX_MODEL);
                    return result;
                }

                if (token_provided) {
                    valid_states.push_back(petri_net_search.current_state());
                } else if (!invalid_state_found) {
                    invalid_state_found = true;
                    invalid_state = petri_net_search.current_state();
                    invalid_state_result = (static_cast<int64_t>(last_read_activity) << 32) + activity_rid;
                }
                petri_net_search.pop_state();
            }

            // Step 2. Explores all valid transitions.
            if (valid_states.empty()) {
                if (invalid_state_found) {
                    // Result: Not conforming. No valid states.
                    petri_net_search.current_state() = invalid_state;
                    temporary_result[index] = invalid_state_result;
                    // Saola implementation throws an exception here if invalid_state_result is less than 1000. It looks like no-op. Removed.
                    last_read_activity = activity_rid;
                    stack.emplace(false, number_of_violations + 1, index + 1);
                } else {
                    // All transitions have been examined. Nothing to do.
                }
            } else if (valid_states.size() == 1) {
                // Result: Conforming. One valid state.
                petri_net_search.current_state() = valid_states[0];
                temporary_result[index] = 0;
                last_read_activity = activity_rid;
                stack.emplace(false, number_of_violations, index + 1);
            } else {
                // Result: Conforming. Multiple valid states. Creates branches.
                temporary_result[index] = 0;
                last_read_activity = activity_rid;
                for (auto it = valid_states.rbegin(); it != valid_states.rend(); it++) {
                    stack.emplace(true, number_of_violations, index + 1, std::move(*it));
                }
            }
        }
    }

    return result;
}

} // namespace starrocks
