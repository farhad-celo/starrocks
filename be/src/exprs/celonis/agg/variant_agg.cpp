#include "variant_agg.h"

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/const_column.h"
#include "exprs/agg/aggregate.h"
#include "gutil/casts.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "runtime/mem_pool.h"

namespace starrocks {

std::pair<int32_t, size_t> VariantAggregateState::maybe_add_activity(MemPool* mem_pool, const Slice& slice,
                                                                     size_t* memory) {
    // TODO(j.kim): Reserve activity id 0 for null to be consistent with Saola.
    int32_t index = 0;
    SliceWithHash key(slice);
    size_t hash = key.hash;

    auto it = activity_map_.find(key, key.hash);
    if (it == activity_map_.end()) {
        // New activity - allocate memory
        char* pos = (char*)mem_pool->allocate(key.size);
        DCHECK(pos != nullptr);
        memcpy(pos, key.data, key.size);
        *memory += phmap::item_serialize_size<SliceHashMap>::value;
        key.data = pos;
        index = activity_map_.size();
        activity_map_.insert(std::pair<SliceWithHash, int32_t>(key, activity_map_.size()));
    } else {
        key.data = it->first.data;
        index = it->second;
    }
    return std::make_pair(index, hash);
}

size_t VariantAggregateState::update(MemPool* mem_pool, const ArrayColumn& activity_column, size_t row_num,
                                     int64_t weight) {
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
        auto idx_hash = maybe_add_activity(mem_pool, b_elements->get_slice(offset), &memory);
        variant.add(idx_hash.first, idx_hash.second);
    }
    // Add the variant into the variant_map.
    variant_map_[variant] += weight;

    return memory;
}

size_t VariantAggregateState::serialized_size() const {
    size_t result = 0;
    result += sizeof(uint32_t); // num_activities

    // activities dictionary
    for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
        result += sizeof(uint32_t); // idx
        result += sizeof(uint32_t); // size
        result += it->first.size;   // data
    }

    // variants
    result += sizeof(uint32_t); // num_variants
    for (auto it = variant_map_.begin(); it != variant_map_.end(); it++) {
        result += sizeof(size_t);                           // count
        result += sizeof(uint32_t);                         // num_activities
        result += sizeof(uint32_t) * it->first.data.size(); // activity indices
    }
    return result;
}

void VariantAggregateState::serialize(uint8_t* dst) const {
    // TODO(hagonzal): Consider proto serialization.

    // serialization format:
    // num_activities
    // (idx size activity) (idx size activity) ...
    // num_variants
    // count num_activities (activity_idx) (activity_idx) ...
    // count num_activities (activity_idx) (activity_idx) ...

    // activities dictionary
    uint32_t num_activities = activity_map_.size();
    memcpy(dst, &num_activities, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = activity_map_.begin(); it != activity_map_.end(); it++) {
        uint32_t size = it->first.size;
        uint32_t idx = it->second;
        memcpy(dst, &idx, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, &size, sizeof(uint32_t));
        dst += sizeof(uint32_t);
        memcpy(dst, it->first.data, it->first.size);
        dst += it->first.size;
    }

    // variants
    uint32_t num_variants = variant_map_.size();
    memcpy(dst, &num_variants, sizeof(uint32_t));
    dst += sizeof(uint32_t);
    for (auto it = variant_map_.begin(); it != variant_map_.end(); it++) {
        size_t count = it->second;
        uint32_t length = it->first.data.size();

        memcpy(dst, &count, sizeof(size_t));
        dst += sizeof(size_t);
        memcpy(dst, &length, sizeof(uint32_t));
        dst += sizeof(uint32_t);

        for (int i = 0; i < it->first.data.size(); i++) {
            uint32_t idx = it->first.data[i];
            memcpy(dst, &idx, sizeof(uint32_t));
            dst += sizeof(uint32_t);
        }
    }
}

size_t VariantAggregateState::deserialize_and_merge(MemPool* mem_pool, const uint8_t* src, size_t len) {
    // read from src and merge with existing state.
    size_t mem = 0;
    const uint8_t* end = src + len;

    // src can contain multiple serialized states. We merge each of them.
    while (src < end) {
        // activities dictionary
        uint32_t num_activities;
        memcpy(&num_activities, src, sizeof(uint32_t));
        src += sizeof(uint32_t);
        // Maps input activity dictionary to the current activity dictionary.
        // src_index -> (local_idx, local_hash)
        std::vector<std::pair<int32_t, size_t>> index_vector;
        index_vector.resize(num_activities);
        for (uint32_t i = 0; i < num_activities; i++) {
            uint32_t len;
            uint32_t idx;
            memcpy(&idx, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            memcpy(&len, src, sizeof(uint32_t));
            src += sizeof(uint32_t);
            Slice s(src, len);
            src += len;
            index_vector[idx] = maybe_add_activity(mem_pool, s, &mem);
        }

        // variants
        uint32_t num_variants;
        memcpy(&num_variants, src, sizeof(uint32_t));
        src += sizeof(uint32_t);

        for (int i = 0; i < num_variants; i++) {
            size_t count;
            uint32_t num_activities;
            memcpy(&count, src, sizeof(size_t));
            src += sizeof(size_t);
            memcpy(&num_activities, src, sizeof(uint32_t));
            src += sizeof(uint32_t);

            Variant variant(num_activities);
            for (int j = 0; j < num_activities; j++) {
                uint32_t idx;
                memcpy(&idx, src, sizeof(uint32_t));
                src += sizeof(uint32_t);
                auto pair = index_vector[idx];
                variant.add(pair.first, pair.second);
            }
            variant_map_[variant] += count;
        }
    }
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

void VariantAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                      size_t row_num) const {
    // Expects columns: [0] variant_column, [1] weight_column
    // Pass a constant column with value 1 to ignore weight.

    if (columns[0]->is_nullable() && columns[0]->is_null(row_num)) {
        return;
    }
    if (columns[1]->is_nullable() && columns[1]->is_null(row_num)) {
        return;
    }
    int64_t weight = 0;
    if (!columns[1]->is_constant()) {
        const auto& w_column = down_cast<const Int64Column&>(*columns[1]);
        weight = w_column.get(row_num).get_int64();
    } else {
        const auto& w_column = down_cast<const ConstColumn&>(*columns[1]);
        weight = w_column.get(0).get_int64();
    }
    const ArrayColumn& activity_column = down_cast<const ArrayColumn&>(*columns[0]);
    this->data(state).update(ctx->mem_pool(), activity_column, row_num, weight);
}

void VariantAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                     size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    DCHECK(column->is_binary());
    const auto* input_column = down_cast<const BinaryColumn*>(column);
    Slice slice = input_column->get_slice(row_num);
    size_t mem_usage = 0;
    mem_usage += this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*)slice.data, slice.size);
    ctx->add_mem_usage(mem_usage);
}

void VariantAggregateFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                   Column* to) const {
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(to);
    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + this->data(state).serialized_size();
    column->get_bytes().resize(new_size);
    this->data(state).serialize(column->get_bytes().data() + old_size);
    column->get_offset().emplace_back(new_size);
}

void VariantAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                  Column* to) const {
    auto finalizer = get_finalizer(ctx, this->data(state));
    std::string s = finalizer->finalize();
    down_cast<BinaryColumn*>(to)->append(s);
}

} // namespace starrocks
