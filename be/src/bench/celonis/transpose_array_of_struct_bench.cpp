#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/datum_tuple.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/transpose_array_of_struct.h"
#include "exprs/function_context.h"
#include "gutil/strings/strcat.h"
#include "runtime/types.h"
#include "testutil/assert.h"

namespace starrocks {

/*
2025-03-25T11:59:43+00:00
Running ./be/build_Release/src/bench/celonis/output/transpose_array_of_struct_bench
Run on (32 X 3291.57 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 512 KiB (x16)
  L3 Unified 32768 KiB (x2)
Load Average: 5.14, 4.33, 3.33
// Number of rows / Number of fields / Array length
------------------------------------------------------------------------------------------------
Benchmark                                      Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------------
BM_TransposeArrayOfStruct/1000/4/10      5215599 ns      5215468 ns          134 RowInvRate=5.21547us
BM_TransposeArrayOfStruct/10000/4/10    51709877 ns     51709458 ns           13 RowInvRate=5.17095us
BM_TransposeArrayOfStruct/1000/8/10      8641812 ns      8641741 ns           82 RowInvRate=8.64174us
BM_TransposeArrayOfStruct/10000/8/10    84378918 ns     84378433 ns            8 RowInvRate=8.43784us
BM_TransposeArrayOfStruct/1000/16/10    14583943 ns     14583564 ns           48 RowInvRate=14.5836us
BM_TransposeArrayOfStruct/10000/16/10  151675492 ns    151665812 ns            5 RowInvRate=15.1666us
BM_TransposeArrayOfStruct/1000/32/10    28195049 ns     28194917 ns           25 RowInvRate=28.1949us
BM_TransposeArrayOfStruct/10000/32/10  296476933 ns    296474030 ns            2 RowInvRate=29.6474us
BM_TransposeArrayOfStruct/1000/4/20     11143703 ns     11143527 ns           63 RowInvRate=11.1435us
BM_TransposeArrayOfStruct/10000/4/20   109373588 ns    109364555 ns            6 RowInvRate=10.9365us
BM_TransposeArrayOfStruct/1000/8/20     18021809 ns     18021833 ns           39 RowInvRate=18.0218us
BM_TransposeArrayOfStruct/10000/8/20   187122477 ns    187122820 ns            4 RowInvRate=18.7123us
BM_TransposeArrayOfStruct/1000/16/20    31425225 ns     31424452 ns           22 RowInvRate=31.4245us
BM_TransposeArrayOfStruct/10000/16/20  345997369 ns    345993912 ns            2 RowInvRate=34.5994us
BM_TransposeArrayOfStruct/1000/32/20    60227327 ns     60220967 ns           11 RowInvRate=60.221us
BM_TransposeArrayOfStruct/10000/32/20  633804468 ns    633785878 ns            1 RowInvRate=63.3786us
*/

TypeDescriptor to_array_of_struct_type(const std::vector<LogicalType>& logical_types) {
    TypeDescriptor struct_type;
    struct_type.type = LogicalType::TYPE_STRUCT;
    for (int i = 0; i < logical_types.size(); ++i) {
        struct_type.children.emplace_back(logical_types[i]);
        struct_type.field_names.emplace_back(StrCat("col", i));
    }
    TypeDescriptor array_type;
    array_type.type = LogicalType::TYPE_ARRAY;
    array_type.children.emplace_back(struct_type);
    return array_type;
}

TypeDescriptor get_return_type(const std::vector<LogicalType>& logical_types) {
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

std::unique_ptr<FunctionContext>
get_ctx(const std::vector<LogicalType>& field_logical_types) {
    std::vector<FunctionContext::TypeDesc> arg_types = {
            AnyValUtil::column_type_to_type_desc(to_array_of_struct_type(field_logical_types))};
    auto return_type = AnyValUtil::column_type_to_type_desc(get_return_type(field_logical_types));
    return std::unique_ptr<FunctionContext>(
            FunctionContext::create_test_context(std::move(arg_types), return_type));
}

void AddRow(const std::vector<std::optional<DatumStruct>>& data, const ColumnPtr& array_of_struct_column) {
    DatumArray array;
    for (auto i = 0; i < data.size(); ++i) {
        if (data[i].has_value()) {
            array.emplace_back(data[i].value());
        } else {
            array.emplace_back(kNullDatum);
        }
    }
    array_of_struct_column->append_datum(array);
}

static void BM_TransposeArrayOfStruct(benchmark::State& state) {
    int num_rows = state.range(0);
    int num_fields = state.range(1);
    int array_length = state.range(2);

    size_t num_values = 1000;
    std::vector<std::string> values;
    values.reserve(num_values);
    for (auto i = 0; i < num_values; ++i) {
        values.push_back("value" + std::to_string(i));
    }

    using UniformInt = std::uniform_int_distribution<int32_t>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_timestamp_increase(0, num_values - 1);

    std::vector<LogicalType> field_logical_types(num_fields, TYPE_VARCHAR);
    auto array_of_struct_type = to_array_of_struct_type(field_logical_types);
    std::unique_ptr<FunctionContext> ctx(get_ctx(field_logical_types));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        ColumnPtr array_of_struct_column = ColumnHelper::create_column(array_of_struct_type, true);
        for (auto i = 0; i < num_rows; i++) {
            DatumArray array;
            array.reserve(array_length);
            for (auto j = 0; j < array_length; ++j) {
                DatumStruct s;
                s.reserve(num_fields);
                for (auto k = 0; k < num_fields; ++k) {
                    s.push_back(Datum(values.at(uniform_timestamp_increase(rng)).data()));
                }
                array.emplace_back(s);
            }
            array_of_struct_column->append_datum(array);
        }
        ctx->set_constant_columns({nullptr});

        state.ResumeTiming();
        EXPECT_TRUE(CelonisTransposeArrayOfStruct::transpose_array_of_struct(ctx.get(), {array_of_struct_column}).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

// Number of rows / Number of fields / Array length
BENCHMARK(BM_TransposeArrayOfStruct)->ArgsProduct({{1000, 10000}, {4, 8, 16, 32}, {10, 20}});

} // namespace starrocks

BENCHMARK_MAIN();
