#include <benchmark/benchmark.h>
#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/calc_throughput.h"
#include "types/logical_type.h"
#include "runtime/types.h"

namespace starrocks {

TypeDescriptor array_type(const LogicalType& element_type) {
    TypeDescriptor t;
    t.type = TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == TYPE_VARCHAR || element_type == TYPE_CHAR) ? 10 : -1;
    return t;
}

static void do_bench(benchmark::State& state, int event_array_size) {
    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_int;
    uniform_int.param(UniformInt::param_type(1, 100'000));

    DatumArray activities;
    DatumArray timestamps;
    activities.resize(event_array_size);
    timestamps.resize(event_array_size);
    std::generate(activities.begin(), activities.end(), [&]() { return uniform_int(rng); });
    std::generate(timestamps.begin(), timestamps.end(), [&]() { return uniform_int(rng); });
    auto start_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    start_label_col->append_datum(Slice("first"));

    auto end_label_col = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false, true, 0);
    end_label_col->append_datum(Slice("first"));
    auto start_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    start_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);

    auto end_activity_col = ColumnHelper::create_column(TypeDescriptor(TYPE_BIGINT), false, true, 0);
    end_activity_col->append_datum(activities[uniform_int(rng) % activities.size()]);
    auto activity_array = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
    activity_array->append_datum(activities);
    auto timestamp_array = ColumnHelper::create_column(array_type(TYPE_BIGINT), false);
    timestamp_array->append_datum(timestamps);

    auto timestamp_cmp = [](const Datum& d1, const Datum& d2) {
        return d1.get_int64() < d2.get_int64();
    };
    std::sort(timestamps.begin(), timestamps.end(), timestamp_cmp);
    for (auto _ : state) {
        auto result = CelonisCalcThroughputFunctions::celonis_calc_throughput(
                nullptr, {activity_array, timestamp_array, start_activity_col, end_activity_col, start_label_col, end_label_col});
    }
}
static void BM_calc_throughput(benchmark::State& state) {
    do_bench(state, state.range(0));
}
BENCHMARK(BM_calc_throughput)->Arg(8)->Arg(64)->Arg(512)->Arg(1024)->Arg(2048);

}  // namespace starrocks

BENCHMARK_MAIN();