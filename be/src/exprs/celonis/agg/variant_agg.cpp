#include "variant_agg.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "exprs/agg/aggregate.h"
#include "exprs/celonis/agg/variant_stats_utils.h"
#include "gutil/casts.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"

namespace starrocks {

size_t VariantAggregateState::update(FunctionContext* ctx, const Column** columns, size_t row_num) {
    // Expects columns: [0] variant_column, [1] weight_column
    // Pass a constant column with value 1 to ignore weight.
    // NULL and NullableColumn are handled by NullableAggregateFunctionVariadic.
    int64_t weight = 0;
    if (!columns[1]->is_constant()) {
        const auto& w_column = down_cast<const Int64Column&>(*columns[1]);
        weight = w_column.get(row_num).get_int64();
    } else {
        const auto& w_column = down_cast<const ConstColumn&>(*columns[1]);
        weight = w_column.get(0).get_int64();
    }

    const ArrayColumn& activity_column = down_cast<const ArrayColumn&>(*columns[0]);
    const UInt32Column::Container& c_offset = activity_column.offsets().get_data();
    const Column* activity_elements = &activity_column.elements();
    const NullableColumn* nc = dynamic_cast<const NullableColumn*>(activity_elements);
    const NullColumn::Container* activity_nulls = nullptr;

    if (activity_elements->has_null()) {
        activity_nulls = &(nc->null_column()->get_data());
    }

    if (nc != nullptr) {
        activity_elements = nc->data_column().get();
    }

    const BinaryColumn* b_elements = down_cast<const BinaryColumn*>(activity_elements);
    size_t memory = 0;
    size_t n = c_offset[row_num + 1] - c_offset[row_num];
    Variant variant(n);

    for (size_t i = 0; i < n; i++) {
        size_t offset = c_offset[row_num] + i;
        if (activity_nulls != nullptr && (*activity_nulls)[offset]) {
            // Note: if we have a, null, b
            // we will consider that a,b form an edge.
            continue;
        }
        auto idx_hash = maybe_add_activity(b_elements->get_slice(offset), ctx->mem_pool(), activity_map_, &memory);
        variant.add(idx_hash.first, idx_hash.second);
    }
    // Add the variant into the variant_map.
    variant_map_[variant] += weight;

    return memory;
}

size_t VariantAggregateState::serialized_size() const {
    return get_serialized_size(activity_map_) + get_serialized_size(variant_map_);
}

void VariantAggregateState::serialize(uint8_t* dst) const {
    dst = serialize_activity_map(dst, activity_map_);
    dst = serialize_variant_map(dst, variant_map_);
}

size_t VariantAggregateState::deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
    // read from src and merge with existing state.
    size_t mem = 0;
    const uint8_t* end = src + len;

    // TODO(xingyuan): figure out this
    // src can contain multiple serialized states. We merge each of them.
    while (src < end) {
        std::vector<std::pair<int32_t, size_t>> index_vector;
        src = deserialize_activity_map_and_merge(src, mem_pool, activity_map_, index_vector, &mem);
        src = deserialize_variant_map_and_merge(src, index_vector, variant_map_);
    }
    DCHECK_EQ(src, end);
    return mem;
}

std::string VariantAggregateState::debug_string() const {
    std::stringstream ss;
    // print the activity_map
    ss << "activity_map\n";
    for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
        ss << it->first.to_string() << ":" << it->second << "\n";
    }

    // print the variant_map
    ss << "variant_map\n";
    for (auto it = variant_map_.begin(); it != variant_map_.end(); it++) {
        ss << it->first.debug_string() << " count " << it->second << "\n";
    }
    return ss.str();
}

} // namespace starrocks
