#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/transits_match.h"
#include "exprs/function_context.h"
#include "gutil/strings/strcat.h"
#include "runtime/types.h"

namespace starrocks {

/*
2025-06-15T16:41:02+00:00
Running ./be/build_Release/src/bench/celonis/output/transits_match_bench
Run on (32 X 3369.09 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 17.73, 12.87, 13.63
// Args: Number of rows / Number of primary key fields / Length of match or manual array
-------------------------------------------------------------------------------------------------
Benchmark                                       Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------
BM_ConstantTransitsMatch/1000/2/10        3185766 ns      3185819 ns          219 RowInvRate=3.18582us
BM_ConstantTransitsMatch/10000/2/10      31523872 ns     31522468 ns           22 RowInvRate=3.15225us
BM_ConstantTransitsMatch/1000/4/10        3422112 ns      3422109 ns          205 RowInvRate=3.42211us
BM_ConstantTransitsMatch/10000/4/10      33791623 ns     33791795 ns           20 RowInvRate=3.37918us
BM_ConstantTransitsMatch/1000/2/20        6596736 ns      6596575 ns          106 RowInvRate=6.59657us
BM_ConstantTransitsMatch/10000/2/20      65784546 ns     65761296 ns           10 RowInvRate=6.57613us
BM_ConstantTransitsMatch/1000/4/20        6980344 ns      6980204 ns          101 RowInvRate=6.9802us
BM_ConstantTransitsMatch/10000/4/20      69432416 ns     69405657 ns           10 RowInvRate=6.94057us
BM_NonConstantTransitsMatch/1000/2/10    25150957 ns     25149751 ns           28 RowInvRate=25.1498us
BM_NonConstantTransitsMatch/10000/2/10  249802242 ns    249787834 ns            3 RowInvRate=24.9788us
BM_NonConstantTransitsMatch/1000/4/10    43736341 ns     43732486 ns           16 RowInvRate=43.7325us
BM_NonConstantTransitsMatch/10000/4/10  443435436 ns    443403747 ns            2 RowInvRate=44.3404us
BM_NonConstantTransitsMatch/1000/2/20    68615490 ns     68615662 ns           10 RowInvRate=68.6157us
BM_NonConstantTransitsMatch/10000/2/20  689181342 ns    689162284 ns            1 RowInvRate=68.9162us
BM_NonConstantTransitsMatch/1000/4/20   127797939 ns    127796881 ns            6 RowInvRate=127.797us
BM_NonConstantTransitsMatch/10000/4/20 1272015929 ns   1271978876 ns            1 RowInvRate=127.198us
*/

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

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

TypeDescriptor get_return_type(const TypeDescriptor& left_key_struct_type,
                               const TypeDescriptor& right_key_struct_type) {
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

std::unique_ptr<FunctionContext> get_ctx(const TypeDescriptor& left_key_struct_type,
                                         const TypeDescriptor& right_key_struct_type) {
    std::vector<FunctionContext::TypeDesc> arg_types = {left_key_struct_type, TYPE_ARRAY_VARCHAR, right_key_struct_type,
                                                        TYPE_ARRAY_VARCHAR,   TYPE_ARRAY_VARCHAR, TYPE_ARRAY_VARCHAR};
    auto return_type = get_return_type(left_key_struct_type, right_key_struct_type);
    return std::unique_ptr<FunctionContext>(FunctionContext::create_test_context(std::move(arg_types), return_type));
}

void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays, const std::optional<DatumArray>& left_match,
            const std::optional<std::vector<DatumArray>>& right_keys_arrays,
            const std::optional<DatumArray>& right_match, ColumnPtr& left_primary_keys_column,
            ColumnPtr& left_match_column, ColumnPtr& right_primary_keys_column, ColumnPtr& right_match_column) {
    auto& left_fields =
            down_cast<StructColumn*>(ColumnHelper::get_data_column(left_primary_keys_column.get()))->fields_column();
    auto left_null_column = down_cast<NullableColumn*>(left_primary_keys_column.get());
    if (left_keys_arrays.has_value()) {
        left_null_column->null_column_data().emplace_back(0);
        for (auto i = 0; i < left_keys_arrays->size(); ++i) {
            left_fields[i]->append_datum(left_keys_arrays->at(i));
        }
    } else {
        left_primary_keys_column->append_datum(kNullDatum);
    }
    if (left_match.has_value()) {
        left_match_column->append_datum(left_match.value());
    } else {
        left_match_column->append_datum(kNullDatum);
    }
    auto& right_fields =
            down_cast<StructColumn*>(ColumnHelper::get_data_column(right_primary_keys_column.get()))->fields_column();
    auto right_null_column = down_cast<NullableColumn*>(right_primary_keys_column.get());
    if (right_keys_arrays.has_value()) {
        right_null_column->null_column_data().emplace_back(0);
        for (auto i = 0; i < right_keys_arrays->size(); ++i) {
            right_fields[i]->append_datum(right_keys_arrays->at(i));
        }
    } else {
        right_primary_keys_column->append_datum(kNullDatum);
    }
    if (right_match.has_value()) {
        right_match_column->append_datum(right_match.value());
    } else {
        right_match_column->append_datum(kNullDatum);
    }
}

void AddRow(const std::optional<std::vector<DatumArray>>& left_keys_arrays, const std::optional<DatumArray>& left_match,
            const std::optional<std::vector<DatumArray>>& right_keys_arrays,
            const std::optional<DatumArray>& right_match, const std::optional<DatumArray>& left_manual,
            const std::optional<DatumArray>& right_manual, ColumnPtr& left_primary_keys_column,
            ColumnPtr& left_match_column, ColumnPtr& right_primary_keys_column, ColumnPtr& right_match_column,
            ColumnPtr& left_manual_column, ColumnPtr& right_manual_column) {
    AddRow(left_keys_arrays, left_match, right_keys_arrays, right_match, left_primary_keys_column, left_match_column,
           right_primary_keys_column, right_match_column);
    if (left_manual.has_value()) {
        left_manual_column->append_datum(left_manual.value());
    } else {
        left_manual_column->append_datum(kNullDatum);
    }
    if (right_manual.has_value()) {
        right_manual_column->append_datum(right_manual.value());
    } else {
        right_manual_column->append_datum(kNullDatum);
    }
}

static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int num_key_fields = state.range(1);
    int array_length = state.range(2);

    size_t num_values = 100;
    std::vector<std::string> left_values;
    left_values.reserve(num_values);
    for (auto i = 0; i < num_values; ++i) {
        left_values.push_back("L" + std::to_string(i));
    }
    std::vector<std::string> right_values;
    right_values.reserve(num_values);
    for (auto i = 0; i < num_values; ++i) {
        right_values.push_back("R" + std::to_string(i));
    }
    DatumArray left_key_array = {};
    for (auto i = 0; i < array_length; ++i) {
        left_key_array.push_back(Slice(left_values[i % num_values]));
    }
    std::optional<std::vector<DatumArray>> left_key_arrays = std::vector<DatumArray>{};
    for (auto i = 0; i < num_key_fields; ++i) {
        left_key_arrays->push_back(left_key_array);
    }
    DatumArray right_key_array = {};
    for (auto i = 0; i < array_length; ++i) {
        right_key_array.push_back(Slice(right_values[i % num_values]));
    }
    std::optional<std::vector<DatumArray>> right_key_arrays = std::vector<DatumArray>{};
    for (auto i = 0; i < num_key_fields; ++i) {
        right_key_arrays->push_back(right_key_array);
    }

    DatumArray left_match_array = left_key_array;
    DatumArray right_match_array = right_key_array;

    std::unique_ptr<FunctionContext> ctx;
    ColumnPtr left_primary_keys_column;
    ColumnPtr left_match_column;
    ColumnPtr right_primary_keys_column;
    ColumnPtr right_match_column;
    ColumnPtr left_manual_column;
    ColumnPtr right_manual_column;

    std::vector<LogicalType> left_logical_types(num_key_fields, TYPE_VARCHAR);
    std::vector<LogicalType> right_logical_types(num_key_fields, TYPE_VARCHAR);

    auto left_key_struct_type = logical_types_to_struct_type(left_logical_types);
    auto right_key_struct_type = logical_types_to_struct_type(right_logical_types);
    ctx = get_ctx(left_key_struct_type, right_key_struct_type);
    auto array_type_desc = logical_type_to_array_type_desc(TYPE_VARCHAR);

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        left_primary_keys_column = ColumnHelper::create_column(left_key_struct_type, true);
        left_match_column = ColumnHelper::create_column(array_type_desc, true);
        right_primary_keys_column = ColumnHelper::create_column(right_key_struct_type, true);
        right_match_column = ColumnHelper::create_column(array_type_desc, true);
        left_manual_column = ColumnHelper::create_column(array_type_desc, true);
        right_manual_column = ColumnHelper::create_column(array_type_desc, true);
        total_rows += num_rows;
        for (int i = 0; i < num_rows; i++) {
            AddRow(left_key_arrays, left_match_array, right_key_arrays, right_match_array, left_match_array,
                   right_match_array, left_primary_keys_column, left_match_column, right_primary_keys_column,
                   right_match_column, left_manual_column, right_manual_column);
        }

        switch (match_type) {
        case CONSTANT:
            ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr, left_manual_column, right_manual_column});
            break;
        case NON_CONSTANT:
            ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
            break;
        }
        state.ResumeTiming();

        ASSERT_TRUE(CelonisTransitsMatch::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisTransitsMatch::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        auto result = CelonisTransitsMatch::transits_match(
                ctx.get(), {left_primary_keys_column, left_match_column, right_primary_keys_column, right_match_column,
                            left_manual_column, right_manual_column});
        ASSERT_TRUE(result.ok()) << result.status().message();
        ASSERT_TRUE(CelonisTransitsMatch::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisTransitsMatch::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_ConstantTransitsMatch(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

static void BM_NonConstantTransitsMatch(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

// Args: Number of rows / Number of primary key fields / Length of match or manual array
BENCHMARK(BM_ConstantTransitsMatch)->ArgsProduct({{1000, 10000}, {2, 4}, {10, 20}});

BENCHMARK(BM_NonConstantTransitsMatch)->ArgsProduct({{1000, 10000}, {2, 4}, {10, 20}});
} // namespace starrocks

BENCHMARK_MAIN();
