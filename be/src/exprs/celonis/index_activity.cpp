#include "exprs/celonis/index_activity.h"

#include "column/array_column.h"
#include "column/column_hash.h"
#include "column/column.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {

enum class Mode {INVALID, ORDER, LOOP, TYPE};

Mode getMode(Slice mode) {
    if (mode == "INDEX_ACTIVITY_ORDER") {
        return Mode::ORDER;
    } else if (mode == "INDEX_ACTIVITY_LOOP") {
        return Mode::LOOP;
    } else if (mode == "INDEX_ACTIVITY_TYPE") {
        return Mode::TYPE;
    }
    return Mode::INVALID;
}

enum class Direction {INVALID, FORWARD, REVERSE};

Direction getDirection(Slice direction) {
    if (direction == "FORWARD") {
        return Direction::FORWARD;
    } else if (direction == "REVERSE") {
        return Direction::REVERSE;
    }
    return Direction::INVALID;
}

template<bool is_reverse>
ColumnPtr index_activity_order_impl(const Column* array_column) {
    UnnestedArrayData array_data = prepare_array_input(array_column);
    const size_t num_rows = array_data.offsets->size() - 1;
    auto offsets_ptr = array_data.offsets->get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(Int64Column::create(), NullColumn::create()),
                                            UInt32Column::create(*array_data.offsets));
    ColumnPtr& result_elements = result_array->elements_column();
    result_elements->reserve(array_data.elements->size());

    std::vector<int64_t> temp_for_reverse;

    for (size_t i = 0; i < num_rows; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((array_data.null_arrays != nullptr && (*array_data.null_arrays)[i]) || array_size == 0) {
            continue;
        }

        if constexpr (is_reverse) {
            temp_for_reverse.clear();
            temp_for_reverse.reserve(array_size);
        }
        int64_t idx = 1;
        for (int j = 0; j < array_size; ++j) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[offset + j] != 0) {
                if constexpr (is_reverse) {
                    temp_for_reverse.emplace_back(-1L);
                } else {
                    result_elements->append_nulls(1);
                }
            } else {
                if constexpr (is_reverse) {
                    temp_for_reverse.emplace_back(idx++);
                } else {
                    result_elements->append_datum(idx++);
                }
            }
        }
        if constexpr (is_reverse) {
            for (auto order : temp_for_reverse) {
                if (order < 0) {
                    result_elements->append_nulls(1);
                } else {
                    result_elements->append_datum(idx - order);
                }
            }
        }
    }
    return result_array;
}

template<bool is_reverse>
ColumnPtr index_activity_loop_impl(const Column* array_column) {
    UnnestedArrayData array_data = prepare_array_input(array_column);
    const auto& elements = *array_data.elements;
    const size_t num_rows = array_data.offsets->size() - 1;
    auto offsets_ptr = array_data.offsets->get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(Int64Column::create(), NullColumn::create()),
                                            UInt32Column::create(*array_data.offsets));
    ColumnPtr& result_elements = result_array->elements_column();
    result_elements->reserve(elements.size());

    std::vector<uint32_t> hash(elements.size(), 0);
    elements.fnv_hash(hash.data(), 0, elements.size());

    std::vector<int64_t> temp_for_reverse;  // -1: NULL, >1 : Max count of a loop starting

    for (size_t i = 0; i < num_rows; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((array_data.null_arrays != nullptr && (*array_data.null_arrays)[i]) || array_size == 0) {
            continue;
        }

        if constexpr (is_reverse) {
            temp_for_reverse.clear();
            temp_for_reverse.resize(array_size);
        }
        size_t loop_start = 0;
        uint32_t loop_hash = 0;
        int64_t count = 0;
        for (int j = 0; j < array_size; ++j) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[offset + j] != 0) {
                if constexpr (is_reverse) {
                    temp_for_reverse[j] = -1L;
                } else {
                    result_elements->append_nulls(1);
                }
            } else {
                if (count == 0 ||
                    !(loop_hash == hash[offset + j] && elements.equals(offset + loop_start, elements, offset + j))) {
                    count = 1L;
                    loop_start = j;
                    loop_hash = hash[offset + j];
                } else {
                    count++;
                    if constexpr (is_reverse) {
                        temp_for_reverse[loop_start] = count;
                    }
                }
                if constexpr (!is_reverse) {
                    result_elements->append_datum(count);
                }
            }
        }
        if constexpr (is_reverse) {
            uint64_t count = 1L;
            for (auto loop : temp_for_reverse) {
                if (loop < 0) {
                    result_elements->append_nulls(1);
                } else {
                    if (loop > 1L) {
                        count = loop;
                    } else if (count > 1L){
                        count--;
                    }
                    result_elements->append_datum(count);
                }
            }
        }
    }
    return result_array;
}

template<bool is_reverse>
ColumnPtr index_activity_type_impl(const Column* array_column) {
    UnnestedArrayData array_data = prepare_array_input(array_column);
    const auto& elements = *array_data.elements;
    const size_t num_rows = array_data.offsets->size() - 1;
    auto offsets_ptr = array_data.offsets->get_data().data();
    auto result_array = ArrayColumn::create(NullableColumn::create(Int64Column::create(), NullColumn::create()),
                                            UInt32Column::create(*array_data.offsets));
    ColumnPtr& result_elements = result_array->elements_column();
    result_elements->reserve(elements.size());

    struct Element {
        const Column* elements;
        uint32_t hash;
        size_t index;
    };

    struct EqualOnElement {
        bool operator()(const Element& x, const Element& y) const {
            return x.hash == y.hash && x.elements->equals(x.index, *y.elements, y.index);
        }
    };

    struct HashOnElement {
        std::size_t operator()(const Element& x) const { return x.hash; }
    };

    phmap::flat_hash_map<Element, int64_t, HashOnElement , EqualOnElement> element_counter_map;

    std::vector<uint32_t> hash(elements.size(), 0);
    elements.fnv_hash(hash.data(), 0, elements.size());

    std::vector<int64_t> temp_for_reverse;  // 0: NULL, >0 : Max count of a loop starting, <0 : -(index of a loop starting + 1)

    for (size_t i = 0; i < num_rows; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if ((array_data.null_arrays != nullptr && (*array_data.null_arrays)[i]) || array_size == 0) {
            continue;
        }

        if constexpr (is_reverse) {
            temp_for_reverse.clear();
            temp_for_reverse.resize(array_size);
        }
        element_counter_map.clear();
        for (int j = 0; j < array_size; ++j) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[offset + j] != 0) {
                if constexpr (is_reverse) {
                    temp_for_reverse[j] = 0;
                } else {
                    result_elements->append_nulls(1);
                }
            } else {
                if constexpr (is_reverse) {
                    auto [it, success] = element_counter_map.insert({{&elements, hash[offset + j], offset + j}, 0});
                    it->second++;
                    if (success) {
                        temp_for_reverse[j] = 1L;
                    } else {
                        temp_for_reverse[it->first.index - offset] = it->second;
                        temp_for_reverse[j] = -(it->first.index - offset + 1L);
                    }
                } else {
                    result_elements->append_datum(++element_counter_map[{&elements, hash[offset + j], offset + j}]);
                }
            }
        }
        if constexpr (is_reverse) {
            for (auto element : temp_for_reverse) {
                if (element == 0) {
                    result_elements->append_nulls(1);
                } else if (element > 0L) {
                    result_elements->append_datum(element);
                } else {
                    result_elements->append_datum(--temp_for_reverse[-element - 1L]);
                }
            }
        }
    }
    return result_array;
}

}  // namespace

struct CelonisIndexActivityStateFragmentLocal {
    ColumnPtr (*function)(const Column*);
};

Status CelonisIndexActivity::celonis_index_activity_prepare(starrocks::FunctionContext *context,
                                                            FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::InvalidArgument(
                    "The second parameter of celonis_index_activity() only accepts a literal value");
        }
        if (!context->is_constant_column(2)) {
            return Status::InvalidArgument(
                    "The third parameter of celonis_index_activity() only accepts a literal value");
        }

        auto state = new CelonisIndexActivityStateFragmentLocal();
        context->set_function_state(scope, state);

        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        if (!context->is_notnull_constant_column(2)) {
            return Status::OK();
        }
        auto mode = getMode(ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(1)));
        auto direction = getDirection(ColumnHelper::get_const_value<TYPE_VARCHAR>(context->get_constant_column(2)));
        if (mode == Mode::ORDER && direction == Direction::FORWARD) {
            state->function = index_activity_order_impl</*is_reverse=*/false>;
        } else if (mode == Mode::ORDER && direction == Direction::REVERSE) {
            state->function = index_activity_order_impl</*is_reverse=*/true>;
        } else if (mode == Mode::LOOP && direction == Direction::FORWARD) {
            state->function = index_activity_loop_impl</*is_reverse=*/false>;
        } else if (mode == Mode::LOOP && direction == Direction::REVERSE) {
            state->function = index_activity_loop_impl</*is_reverse=*/true>;
        } else if (mode == Mode::TYPE && direction == Direction::FORWARD) {
            state->function = index_activity_type_impl</*is_reverse=*/false>;
        } else if (mode == Mode::TYPE && direction == Direction::REVERSE) {
            state->function = index_activity_type_impl</*is_reverse=*/true>;
        } else {
            return Status::InvalidArgument("unsupported mode or direction in celonis_index_activity()");
        }
    }

    return Status::OK();
}

Status CelonisIndexActivity::celonis_index_activity_close(starrocks::FunctionContext *context,
                                                          FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state_fragment_local = reinterpret_cast<const CelonisIndexActivityStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state_fragment_local;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisIndexActivity::celonis_index_activity(FunctionContext* context,
                                                                 const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const Column* array_column = columns[0].get();
    const auto* state = reinterpret_cast<const CelonisIndexActivityStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto result = state->function(array_column);
    if (array_column->has_null()) {
        return NullableColumn::create(std::move(result),
                                      down_cast<const NullableColumn*>(array_column)->null_column());
    }
    return result;
}

} // namespace starrocks
