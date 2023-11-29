#include "mode_agg.h"

#include "column/const_column.h"
#include "exprs/celonis/serialization_utils.h"
#include "runtime/mem_pool.h"

namespace starrocks {

template <LogicalType LT>
void CelonisModeState<LT>::increment_occurrence([[maybe_unused]] FunctionContext* const ctx, ValueType value,
                                                const OccurrenceCountType count) {
    if constexpr (HAS_SLICE_VALUE_TYPE) {
        if (!aggregate_.contains(value)) {
            const auto bfr_size{value.size};
            // New string value not yet stored in the map - allocate memory for it
            char* bfr{reinterpret_cast<char*>(ctx->mem_pool()->allocate(bfr_size))};
            std::memcpy(bfr, value.data, bfr_size);
            value.data = bfr;
            ctx->add_mem_usage(bfr_size);
        }
    }
    aggregate_[value] += count;
}

template <LogicalType LT>
[[nodiscard]] std::optional<const typename CelonisModeState<LT>::ValueType>
CelonisModeState<LT>::most_frequent_or_null() const {
    const auto is_lt{[](const auto& lhs, const auto& rhs) {
        static_assert(std::is_same_v<decltype(lhs), decltype(rhs)>);
        return lhs < rhs;
    }};
    OccurrenceCountType current_max{0};
    std::optional<const ValueType> result{std::nullopt};
    for (const auto& [value, count] : aggregate_) {
        if (count > current_max || (count == current_max && is_lt(value, *result))) {
            result.emplace(value);
            current_max = count;
        }
    }
    return result;
}

template <LogicalType LT>
std::size_t CelonisModeState<LT>::serialization_size() const {
    std::size_t required_bytes_for_serialization{0};
    required_bytes_for_serialization += sizeof(decltype(aggregate_.size())); // # entries
    // for each entry
    for (const auto& [value, count] : aggregate_) {
        required_bytes_for_serialization += sizeof(OccurrenceCountType);                     // count
        required_bytes_for_serialization += starrocks::serialization_size<ValueType>(value); // value
    }
    return required_bytes_for_serialization;
}

template <LogicalType LT>
void CelonisModeState<LT>::serialize_to_dst(uint8_t* dst) const {
    // serialization format:
    // [# entries]
    // For BIGINT/DOUBLE/DATETIME
    // [(count, value )] ...
    // For VARCHAR
    // [(count, string size, chars )] ...
    serialize<std::size_t>(dst, aggregate_.size());
    for (const auto& [value, count] : aggregate_) {
        serialize<OccurrenceCountType>(dst, count);
        serialize<ValueType>(dst, value);
    }
}

template <LogicalType LT>
void CelonisModeState<LT>::deserialize_from_src_and_merge(FunctionContext* const ctx, const Slice& src) {
    const auto buffer_size{src.size};
    auto* buffer{reinterpret_cast<ByteBuffer>(src.data)};
    const auto* buffer_end{buffer + buffer_size};

    // src can contain multiple serialized states. We merge each of them.
    while (buffer < buffer_end) {
        // serialization format:
        // [# entries]
        // For BIGINT/DOUBLE/DATETIME
        // [(count, value )] ...
        // For VARCHAR
        // [(count, string size, chars )] ...
        const auto number_of_entries{deserialize<std::size_t>(buffer)};
        for (std::size_t entry_idx{0}; entry_idx < number_of_entries; ++entry_idx) {
            const auto count{deserialize<OccurrenceCountType>(buffer)};
            const auto value{deserialize<ValueType>(buffer)};
            increment_occurrence(ctx, value, count);
        }
    }
    DCHECK(buffer == buffer_end);
}

template <LogicalType LT>
void CelonisModeAggregateFunction<LT>::update(FunctionContext* const ctx, const Column** const columns,
                                              AggDataPtr __restrict state, const size_t row_num) const {
    const auto& input_column{down_cast<const NullableColumn&>(**columns)};
    if (!input_column.is_null(row_num)) {
        const Datum& value{input_column.data_column()->get(row_num)};
        this->data(state).increment_occurrence(ctx, value.get<RunTimeCppType<LT>>());
    }
}

template <LogicalType LT>
void CelonisModeAggregateFunction<LT>::merge(FunctionContext* const ctx, const Column* column,
                                             AggDataPtr __restrict state, const size_t row_num) const {
    const auto& serialized_input_column_to_merge{down_cast<const BinaryColumn&>(*column)};
    const auto slice{serialized_input_column_to_merge.get_slice(row_num)};
    this->data(state).deserialize_from_src_and_merge(ctx, slice);
}

template <LogicalType LT>
void CelonisModeAggregateFunction<LT>::serialize_to_column([[maybe_unused]] FunctionContext* ctx,
                                                           ConstAggDataPtr __restrict state, Column* const to) const {
    const CelonisModeState<LT>& typed_state{this->data(state)};
    auto& column{down_cast<BinaryColumn&>(*to)};
    auto& column_bytes{column.get_bytes()};
    std::size_t old_size{column_bytes.size()};
    std::size_t new_size{old_size + typed_state.serialization_size()};
    column_bytes.resize(new_size);
    typed_state.serialize_to_dst(column_bytes.data() + old_size);
    column.get_offset().emplace_back(new_size);
}

template <LogicalType LT>
void CelonisModeAggregateFunction<LT>::finalize_to_column([[maybe_unused]] FunctionContext* const ctx,
                                                          ConstAggDataPtr __restrict state, Column* const to) const {
    const auto optional_final_result{this->data(state).most_frequent_or_null()};
    auto& result_column{down_cast<NullableColumn&>(*to)};
    result_column.append_datum(optional_final_result.has_value() ? Datum{*optional_final_result} : kNullDatum);
}

template class CelonisModeState<TYPE_BIGINT>;
template class CelonisModeState<TYPE_DOUBLE>;
template class CelonisModeState<TYPE_VARCHAR>;
template class CelonisModeState<TYPE_DATETIME>;

template class CelonisModeAggregateFunction<TYPE_BIGINT>;
template class CelonisModeAggregateFunction<TYPE_DOUBLE>;
template class CelonisModeAggregateFunction<TYPE_VARCHAR>;
template class CelonisModeAggregateFunction<TYPE_DATETIME>;

} // namespace starrocks
