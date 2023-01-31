#include <benchmark/benchmark.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/table_function/table_function.h"
#include "exprs/celonis/count_edges.h"
#include "runtime/types.h"
#include "types/logical_type.h"

namespace starrocks {

TypeDescriptor array_type(const LogicalType& element_type) {
    starrocks::TypeDescriptor t;
    t.type = starrocks::TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == starrocks::TYPE_VARCHAR || element_type == starrocks::TYPE_CHAR) ? 10 : -1;
    return t;
}

static void do_bench(benchmark::State& state, int array_size, int num_rows) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 250));

    std::vector<std::string> activities;
    for (int i = 0; i < array_size/2; ++i) {
        std::stringstream str;
        str << "ACTIVITY_NUMBER_" << i;
        activities.push_back(str.str());
    }
    auto gen_rand_vector = [&](int max_length) {
        int array_len = 1 + uniform_int(rng) % max_length;
        DatumArray result;
        for (int i = 0; i < array_len; ++i) {
            result.push_back(activities[uniform_int(rng) % activities.size()]);
        }
        return result;
    };
    for (auto _ : state) {
        state.PauseTiming();
        ColumnPtr activity_column = ColumnHelper::create_column(array_type(TYPE_VARCHAR), false);
        for (int i = 0; i < num_rows; ++i) {
            activity_column->append_datum(gen_rand_vector(array_size));
        }
        TableFunctionState* table_state;
        auto function = std::make_unique<CountEdges>();
        Columns input;
        input.push_back(activity_column);
        function->init({}, &table_state);
        table_state->set_params(input);
        table_state->set_params(input);
        bool eos = false;
        state.ResumeTiming();
        function->process(table_state, &eos);
        function->close(nullptr, table_state);
    }
}

static void BM_count_edges(benchmark::State& state) {
    do_bench(state, state.range(0), state.range(1));
}

BENCHMARK(BM_count_edges)->Args({8, 1000})->Args({8, 5000})->Args({8, 10000})->Args({8, 200000})
        ->Args({16, 1000})->Args({16, 5000})->Args({16, 10000})->Args({16, 200000})
        ->Args({32, 1000})->Args({32, 5000})->Args({32, 10000})->Args({32, 200000})
        ->Args({64, 1000})->Args({64, 5000})->Args({64, 10000})->Args({64, 200000})
        ->Args({128, 1000})->Args({128, 5000})->Args({128, 10000})->Args({128, 200000});

}  // namespace starrocks

BENCHMARK_MAIN();
