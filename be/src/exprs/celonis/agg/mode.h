#pragma once

#include <optional>
#include <type_traits>

#include "column/column.h"
#include "column/column_hash.h"
#include "column/nullable_column.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "types/logical_type.h"
#include "util/phmap/phmap.h"

namespace starrocks {

template <LogicalType LT>
class CelonisModeState {
public:
    static_assert(LT == TYPE_BIGINT || LT == TYPE_DOUBLE || LT == TYPE_VARCHAR || LT == TYPE_DATETIME);
    using ValueType = RunTimeCppType<LT>;
    using OccurrenceCountType = std::size_t;

    CelonisModeState() = default;

    /**
      * @brief Increments the occurrence of the given value.
      * @note If the value was not yet stored and is a string, allocates memory in the current given function context
      */
    void increment_occurrence(FunctionContext* ctx, ValueType value, OccurrenceCountType count = 1);
    /**
      * @brief Returns the currently most frequently occurring value or std::nullopt if there are no values stored
      * @note If multiple values have the same number of occurrences, the smaller value is returned as a tie breaker
      */
    [[nodiscard]] std::optional<const ValueType> most_frequent_or_null() const;
    /** Returns the total number of required bytes to serialize this state */
    [[nodiscard]] std::size_t serialization_size() const;
    /** Serializes the current state into the given buffer (at least serialization_size() bytes are required) */
    void serialize_to_dst(uint8_t* dst) const;
    /** Deserializes the serialized state stored in the given slice and merges the result with the current state */
    void deserialize_from_src_and_merge(FunctionContext* ctx, const Slice& src);

private:
    static constexpr bool HAS_SLICE_VALUE_TYPE{std::is_same_v<ValueType, Slice>};
    using MapType = std::conditional_t<
            HAS_SLICE_VALUE_TYPE,
            phmap::flat_hash_map<ValueType, OccurrenceCountType, SliceHash, SliceEqual>, // for type VARCHAR
            phmap::flat_hash_map<ValueType, OccurrenceCountType, StdHash<ValueType>>>;   // for other supported types
    /* Stores the number of occurrences for each value */
    MapType aggregate_{};
};

/**
 * @param: [ input_column ]
 * @paramType columns: [ BIGINT | DOUBLE | VARCHAR | DATETIME ]
 * @return: input_column type
 * Supports PQL MODE https://docs.celonis.com/en/mode.html
 * - The result is only null if all input is null. E.g., [null, null, null] -> [null] but [null, null, A] -> [A]
 * - In PQL for empty input the output is null
 */
template <LogicalType LT>
class CelonisModeAggregateFunction final
        : public AggregateFunctionBatchHelper<CelonisModeState<LT>, CelonisModeAggregateFunction<LT>> {
public:
    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override;
    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;
    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;
    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;
    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;
    [[nodiscard]] std::string get_name() const override;
};

} // namespace starrocks
