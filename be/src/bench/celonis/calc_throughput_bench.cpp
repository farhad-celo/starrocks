#include <benchmark/benchmark.h>
#include <random>
#include <cassert>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/calc_throughput.h"
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

static void do_bench(benchmark::State& state, LogicalType activity_type, int event_array_size) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 250));

    DatumArray activities;
    DatumArray timestamps;
    activities.resize(event_array_size);
    timestamps.resize(event_array_size);
    static std::string alphanum =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    auto gen_rand_str = [&](int max_length) {
        int str_len = 1 + uniform_int(rng) % max_length;
        int str_start = std::min(uniform_int(rng) % alphanum.size(), alphanum.size() - str_len);
        return Slice(alphanum.c_str() + str_start, str_len);
    };
    ColumnPtr activity_array = nullptr;
    ColumnPtr start_activity_col, end_activity_col;
    if (activity_type == TYPE_VARCHAR) {
        int max_str_length = state.range(1);
        assert(max_str_length < alphanum.size());
        std::generate(activities.begin(), activities.end(), [&]() { return gen_rand_str(max_str_length); });
        activity_array = ColumnHelper::create_column(array_type(TYPE_VARCHAR), false);
        activity_array->append_datum(activities);
        start_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
        end_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    } else if (activity_type == TYPE_BIGINT) {
        std::generate(activities.begin(), activities.end(), [&]() { return uniform_int(rng); });
        activity_array = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
        activity_array->append_datum(activities);
        start_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
        end_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    } else {
        std::cerr << "activity type not supported" << std::endl;
    }

    std::generate(timestamps.begin(), timestamps.end(), [&]() { return uniform_int(rng); });
    auto start_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    start_label_col->append_datum(Slice("first"));

    auto end_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    end_label_col->append_datum(Slice("first"));
    auto timestamp_array = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
    timestamp_array->append_datum(timestamps);

    auto timestamp_cmp = [](const Datum& d1, const Datum& d2) {
        return d1.get_int64() < d2.get_int64();
    };
    std::sort(timestamps.begin(), timestamps.end(), timestamp_cmp);
    for (auto _ : state) {
        start_activity_col->resize(0);
        start_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
        end_activity_col->resize(0);
        end_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
        auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
                nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col});
    }
}
static void BM_calc_throughput_int(benchmark::State& state) {
    do_bench(state, TYPE_BIGINT, state.range(0));
}

static void BM_calc_throughput_str(benchmark::State& state) {
    do_bench(state, TYPE_VARCHAR, state.range(0));
    int x = 1; x++;
}

BENCHMARK(BM_calc_throughput_int)->Arg(8)->Arg(64)->Arg(512)->Arg(1024)->Arg(2048);

BENCHMARK(BM_calc_throughput_str)->Args({8, 3})->Args({8, 10})->Args({8, 20})->Args({8, 50})
        ->Args({64, 3})->Args({64, 10})->Args({64, 20})->Args({64, 50})
        ->Args({128, 3})->Args({128, 10})->Args({128, 20})->Args({128, 50})
        ->Args({512, 3})->Args({512, 10})->Args({512, 20})->Args({512, 50})
        ->Args({1024, 3})->Args({1024, 10})->Args({1024, 20})->Args({1024, 50})
        ->Args({2048, 3})->Args({2048, 10})->Args({2048, 20})->Args({2048, 50});

}  // namespace starrocks

BENCHMARK_MAIN();
