#include "count_edges.h"

#include "column/column_helper.h"
#include "column/nullable_column.h"
#include "exprs/celonis/util.h"
#include "exprs/table_function/table_function.h"
#include "util/hash_util.hpp"

namespace starrocks {

namespace {
struct SlicePairWithHash {
    size_t hash;
    const Slice& src;
    const Slice& target;

    SlicePairWithHash(const Slice& src, const Slice& target) : src(src), target(target) {
        hash = SliceHash()(src);
        // TODO: figure out hash_combine usage.
        // Formula is taken from HashUtil::hash_combine, we can't use it directly because std::hash is not defined
        // for Slice.
        hash ^= SliceHash()(target) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
};

class EqualOnSlicePairWithHash {
public:
    bool operator()(const SlicePairWithHash& x, const SlicePairWithHash& y) const {
        return x.hash == y.hash && x.src == y.src && x.target == y.target;
    }
};

class HashOnSlicePairWithHash {
public:
    std::size_t operator()(const SlicePairWithHash& slice) const { return slice.hash; }
};

template <bool element_has_null>
std::pair<Columns, UInt32Column::Ptr> process_impl(TableFunctionState* state, const Column& elements,
                                                   const UInt32Column& offsets,
                                                   const NullColumn::Container* null_offsets,
                                                   const NullColumn::Container* activity_array_nulls) {
    const size_t num_rows = offsets.size() - 1;
    auto offsets_ptr = offsets.get_data().data();
    // TODO: other types
    using ValueType = RunTimeCppType<TYPE_VARCHAR>;
    auto elements_ptr = (const ValueType*)(elements.raw_data());

    Columns result;
    auto offset_column = UInt32Column::create();
    int result_offset = 0;
    offset_column->append(result_offset);
    auto source_column_ptr = BinaryColumn::create();
    auto target_column_ptr = BinaryColumn::create();
    auto count_column_ptr = UInt64Column::create();
    result.emplace_back(source_column_ptr);
    result.emplace_back(target_column_ptr);
    result.emplace_back(count_column_ptr);

    using SliceCountHashMap =
            phmap::flat_hash_map<SlicePairWithHash, int64_t, HashOnSlicePairWithHash, EqualOnSlicePairWithHash>;
    SliceCountHashMap edges_count;
    std::vector<SlicePairWithHash> distinct_edges;
    for (size_t i = 0; i < num_rows; i++) {
        if (activity_array_nulls != nullptr && (*activity_array_nulls)[i]) {
            // Skip the NULL array.
            offset_column->append(result_offset);
            continue;
        }
        edges_count.clear();
        distinct_edges.clear();
        size_t offset = offsets_ptr[i];
        int64_t array_size = offsets_ptr[i + 1] - offsets_ptr[i];
        if (array_size <= 1) {
            offset_column->append(result_offset);
            continue;
        }
        for (auto index = 0; index < array_size - 1; ++index) {
            if constexpr (element_has_null) {
                // Nulls are ignored
                if ((*null_offsets)[offset + index] != 0) {
                    continue;
                }
            }
            const auto& value = elements_ptr[offset + index];
            if constexpr (element_has_null) {
                while (index < array_size - 1 && (*null_offsets)[offset + index + 1] != 0) {
                    index++;
                }
                if (index == array_size - 1) {
                    continue;
                }
            }
            const auto& next_value = elements_ptr[offset + index + 1];
            SlicePairWithHash candidate_edge(value, next_value);
            auto [it, inserted] = edges_count.try_emplace(candidate_edge, 0);
            it->second++;
            if (inserted) {
                distinct_edges.push_back(candidate_edge);
                result_offset++;
            }
        }
        offset_column->append(result_offset);
        for (const auto& edge : distinct_edges) {
            source_column_ptr->append(edge.src);
            target_column_ptr->append(edge.target);
            count_column_ptr->append(edges_count[edge]);
        }
    }
    state->set_offset(result_offset);

    return std::make_pair(result, UInt32Column::Ptr(std::move(offset_column)));
}
} // namespace

std::pair<Columns, UInt32Column::Ptr> CountEdges::process(RuntimeState* runtime_state,
                                                          TableFunctionState* state) const {
    if (state->get_columns().empty()) {
        return {};
    }
    const Column* activity_array = state->get_columns()[0].get();
    state->set_processed_rows(activity_array->size());

    const NullableColumn* nullable_activity_array = nullptr;
    const NullColumn::Container* activity_array_nulls = nullptr;

    if (activity_array->is_nullable()) {
        nullable_activity_array = down_cast<const NullableColumn*>(activity_array);
        activity_array = nullable_activity_array->data_column().get();
        activity_array_nulls = &(nullable_activity_array->null_column()->get_data());
    }

    const auto& activity_array_column = extract_array_column(activity_array);
    const UInt32Column& activity_offsets = activity_array_column.offsets();
    const Column* activity_elements = &activity_array_column.elements();
    const NullColumn::Container* activity_nulls = nullptr;
    if (activity_elements->has_null()) {
        activity_nulls = &(down_cast<const NullableColumn*>(activity_elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(activity_elements); nullable != nullptr) {
        activity_elements = nullable->data_column().get();
    }

    if (activity_nulls != nullptr) {
        return process_impl<true>(state, *activity_elements, activity_offsets, activity_nulls, activity_array_nulls);
    }
    return process_impl<false>(state, *activity_elements, activity_offsets, activity_nulls, activity_array_nulls);
}

} // namespace starrocks
