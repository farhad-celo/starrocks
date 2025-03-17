#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/transits_interleaved.h"
#include "exprs/function_context.h"
#include "gutil/strings/strcat.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-03-16T15:10:54+00:00
Running ./be/build_Release/src/bench/celonis/output/transits_interleaved_bench
Run on (32 X 2445.43 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 2.64, 1.43, 0.68
--------------------------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------
BM_TransitsInterleaved/1000/2/10    28589467 ns     28588587 ns           25 RowInvRate=28.5886us
BM_TransitsInterleaved/10000/2/10  285495344 ns    285484677 ns            2 RowInvRate=28.5485us
BM_TransitsInterleaved/1000/4/10    53383700 ns     53381811 ns           13 RowInvRate=53.3818us
BM_TransitsInterleaved/10000/4/10  532756552 ns    532743409 ns            1 RowInvRate=53.2743us
BM_TransitsInterleaved/1000/2/20    88563445 ns     88554559 ns            8 RowInvRate=88.5546us
BM_TransitsInterleaved/10000/2/20  891932434 ns    891890718 ns            1 RowInvRate=89.1891us
BM_TransitsInterleaved/1000/4/20   171559885 ns    171549020 ns            4 RowInvRate=171.549us
BM_TransitsInterleaved/10000/4/20 1700078437 ns   1699893928 ns            1 RowInvRate=169.989us
*/

TypeDescriptor TYPE_ARRAY_DATETIME = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DATETIME));
TypeDescriptor TYPE_ARRAY_VARCHAR = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_VARCHAR));
TypeDescriptor TYPE_ARRAY_BIGINT = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_BIGINT));
TypeDescriptor TYPE_ARRAY_DOUBLE = TypeDescriptor::create_array_type(TypeDescriptor(TYPE_DOUBLE));

TypeDescriptor logical_type_to_array_type_desc(LogicalType logical_type) {
    if (logical_type == TYPE_VARCHAR) {
        return TYPE_ARRAY_VARCHAR;
    } else if (logical_type == TYPE_DATETIME) {
        return TYPE_ARRAY_DATETIME;
    } else if (logical_type == TYPE_BIGINT) {
        return TYPE_ARRAY_BIGINT;
    } else if (logical_type == TYPE_DOUBLE) {
        return TYPE_ARRAY_DOUBLE;
    }
    return TYPE_ARRAY_VARCHAR;
}

TypeDescriptor logical_types_to_struct_type(const std::vector<LogicalType>& logical_types) {
    TypeDescriptor struct_type;
    struct_type.type = LogicalType::TYPE_STRUCT;
    for (int i = 0; i < logical_types.size(); ++i) {
        TypeDescriptor array_type;
        array_type.type = LogicalType::TYPE_ARRAY;
        array_type.children.emplace_back(logical_types[i]);
        struct_type.children.emplace_back(array_type);
        struct_type.field_names.emplace_back(StrCat("col", i));
    }
    return struct_type;
}

TypeDescriptor
get_return_type(const TypeDescriptor& left_key_struct_type, const TypeDescriptor& right_key_struct_type) {
    TypeDescriptor struct_type;
    struct_type.type = LogicalType::TYPE_STRUCT;

    struct_type.children.emplace_back(LogicalType::TYPE_STRUCT);
    struct_type.field_names.emplace_back("left");
    struct_type.children.emplace_back(LogicalType::TYPE_STRUCT);
    struct_type.field_names.emplace_back("right");

    for (auto i = 0; i < left_key_struct_type.children.size(); ++i) {
        struct_type.children[0].field_names.emplace_back(StrCat("Col ", left_key_struct_type.field_names[i]));
        struct_type.children[0].children.emplace_back(left_key_struct_type.children[i]);
    }
    for (auto i = 0; i < right_key_struct_type.children.size(); ++i) {
        struct_type.children[1].field_names.emplace_back(StrCat("Col ", right_key_struct_type.field_names[i]));
        struct_type.children[1].children.emplace_back(right_key_struct_type.children[i]);
    }
    return struct_type;
}

std::unique_ptr<FunctionContext>
get_ctx(const TypeDescriptor& left_key_struct_type, const TypeDescriptor& right_key_struct_type) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(left_key_struct_type),
            AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_DATETIME),
            AnyValUtil::column_type_to_type_desc(right_key_struct_type),
            AnyValUtil::column_type_to_type_desc(TYPE_ARRAY_DATETIME),
            AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_BOOLEAN))
    };
    auto return_type = AnyValUtil::column_type_to_type_desc(
            get_return_type(left_key_struct_type, right_key_struct_type));
    return std::unique_ptr<FunctionContext>(
            FunctionContext::create_test_context(std::move(arg_types), return_type));
}

void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays,
            const std::optional<DatumArray>& left_timestamps,
            const std::optional<DatumArray>& left_sortings,
            const std::optional<std::vector<DatumArray>>& right_keys_arrays,
            const std::optional<DatumArray>& right_timestamps,
            const std::optional<DatumArray>& right_sortings,
            std::optional<bool> first_last_only, const ColumnPtr& left_primary_keys_column,
            const ColumnPtr& left_timestamps_column, const ColumnPtr& left_sortings_column,
            const ColumnPtr& right_primary_keys_column, const ColumnPtr& right_timestamps_column,
            const ColumnPtr& right_sortings_column, const ColumnPtr& first_last_only_column) {
    auto& left_fields = down_cast<StructColumn*>(
            ColumnHelper::get_data_column(left_primary_keys_column.get()))->fields_column();
    auto left_null_column = down_cast<NullableColumn*>(left_primary_keys_column.get());
    if (left_keys_arrays.has_value()) {
        left_null_column->null_column_data().emplace_back(0);
        for (auto i = 0; i < left_keys_arrays->size(); ++i) {
            left_fields[i]->append_datum(left_keys_arrays->at(i));
        }
    } else {
        left_primary_keys_column->append_datum(kNullDatum);
    }
    if (left_timestamps.has_value()) {
        left_timestamps_column->append_datum(left_timestamps.value());
    } else {
        left_timestamps_column->append_datum(kNullDatum);
    }
    if (left_sortings.has_value()) {
        left_sortings_column->append_datum(left_sortings.value());
    } else {
        left_sortings_column->append_datum(kNullDatum);
    }
    auto& right_fields = down_cast<StructColumn*>(
            ColumnHelper::get_data_column(right_primary_keys_column.get()))->fields_column();
    auto right_null_column = down_cast<NullableColumn*>(right_primary_keys_column.get());
    if (right_keys_arrays.has_value()) {
        right_null_column->null_column_data().emplace_back(0);
        for (auto i = 0; i < right_keys_arrays->size(); ++i) {
            right_fields[i]->append_datum(right_keys_arrays->at(i));
        }
    } else {
        right_primary_keys_column->append_datum(kNullDatum);
    }
    if (right_timestamps.has_value()) {
        right_timestamps_column->append_datum(right_timestamps.value());
    } else {
        right_timestamps_column->append_datum(kNullDatum);
    }
    if (right_sortings.has_value()) {
        right_sortings_column->append_datum(right_sortings.value());
    } else {
        right_sortings_column->append_datum(kNullDatum);
    }
    if (first_last_only.has_value()) {
        first_last_only_column->append_datum(first_last_only.value());
    } else {
        first_last_only_column->append_datum(kNullDatum);
    }
}

static void BM_TransitsInterleaved(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_key_fields = state.range(1);
    int array_length = state.range(2);

    size_t num_values = 100;
    std::vector<std::string> values;
    values.reserve(num_values);
    for (auto i = 0; i < num_values; ++i) {
        values.push_back("V" + std::to_string(i));
    }
    DatumArray key_array = {};
    for (auto i = 0; i < array_length; ++i) {
        key_array.push_back(Slice(values[i % num_values]));
    }
    std::optional<std::vector<DatumArray>> key_arrays = std::vector<DatumArray>{};
    for (auto i = 0; i < num_key_fields; ++i) {
        key_arrays->push_back(key_array);
    }
    DatumArray left_sortings = {};
    for (auto i = 0; i < array_length; ++i) {
        left_sortings.push_back(1L);
    }
    DatumArray right_sortings = {};
    for (auto i = 0; i < array_length; ++i) {
        right_sortings.push_back(1L);
    }
    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_timestamp_increase(1, 1000);

    std::unique_ptr<FunctionContext> ctx;
    ColumnPtr left_primary_keys_column;
    ColumnPtr left_timestamps_column;
    ColumnPtr left_sortings_column;
    ColumnPtr right_primary_keys_column;
    ColumnPtr right_timestamps_column;
    ColumnPtr right_sortings_column;
    ColumnPtr first_last_only_column;

    std::vector<LogicalType> logical_types(num_key_fields, TYPE_VARCHAR);
    auto left_key_struct_type = logical_types_to_struct_type(logical_types);
    auto right_key_struct_type = logical_types_to_struct_type(logical_types);
    auto array_type_desc = logical_type_to_array_type_desc(TYPE_BIGINT);
    ctx = get_ctx(left_key_struct_type, right_key_struct_type);

    int total_rows = 0;
    TimestampValue timestamp;
    for (auto _: state) {
        state.PauseTiming();
        left_primary_keys_column = ColumnHelper::create_column(left_key_struct_type, true);
        left_timestamps_column = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
        left_sortings_column = ColumnHelper::create_column(array_type_desc, true);
        right_primary_keys_column = ColumnHelper::create_column(right_key_struct_type, true);
        right_timestamps_column = ColumnHelper::create_column(TYPE_ARRAY_DATETIME, true);
        right_sortings_column = ColumnHelper::create_column(array_type_desc, true);
        first_last_only_column = ColumnHelper::create_column(TypeDescriptor(TYPE_BOOLEAN), true);
        total_rows += num_rows;
        for (int i = 0; i < num_rows; i++) {
            DatumArray left_timestamps;
            DatumArray right_timestamps;
            int64_t unix_timestamp = 1672560000; // 2023-01-01
            for (auto j = 0; j < array_length; ++j) {
                unix_timestamp += uniform_timestamp_increase(rng);
                timestamp.from_unix_second(unix_timestamp);
                left_timestamps.emplace_back(timestamp);
            }
            unix_timestamp = 1672560000; // 2023-01-01
            for (auto j = 0; j < array_length; ++j) {
                unix_timestamp += uniform_timestamp_increase(rng);
                timestamp.from_unix_second(unix_timestamp);
                right_timestamps.emplace_back(timestamp);
            }
            AddRow(key_arrays, left_timestamps, left_sortings, key_arrays, right_timestamps, right_sortings,
                   false, left_primary_keys_column,
                   left_timestamps_column,
                   left_sortings_column,
                   right_primary_keys_column,
                   right_timestamps_column,
                   right_sortings_column,
                   first_last_only_column);
        }
        state.ResumeTiming();
        auto result = CelonisTransitsInterleaved::transits_interleaved(ctx.get(), {left_primary_keys_column,
                                                                                   left_timestamps_column,
                                                                                   left_sortings_column,
                                                                                   right_primary_keys_column,
                                                                                   right_timestamps_column,
                                                                                   right_sortings_column,
                                                                                   first_last_only_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Args: Number of rows / Number of primary key fields / Length of timestamp (sorting) array
BENCHMARK(BM_TransitsInterleaved)->ArgsProduct({{1000, 10000}, {2, 4}, {10, 20}});

} // namespace starrocks

BENCHMARK_MAIN();
