#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "exprs/celonis/agg/percentile_disc.h"

namespace starrocks {

/*
2026-03-09T08:22:54+00:00
Running ./be/build_Release/src/bench/celonis/output/percentile_disc_bench
Run on (16 X 4900 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1280 KiB (x8)
  L3 Unified 24576 KiB (x1)
Load Average: 9.44, 3.61, 2.37
------------------------------------------------------------------------------------------------------------------
Benchmark                                                                        Time             CPU   Iterations
------------------------------------------------------------------------------------------------------------------
BM_percentile_disc_flatten_and_sort/10/10000/1/1000000000                     2.23 ms         2.23 ms          297
BM_percentile_disc_flatten_and_sort/50/10000/1/1000000000                     12.0 ms         12.0 ms           60
BM_percentile_disc_flatten_and_sort/100/10000/1/1000000000                    25.5 ms         25.5 ms           27
BM_percentile_disc_flatten_and_sort/10/100000/1/1000000000                    35.3 ms         35.3 ms           29
BM_percentile_disc_flatten_and_sort/50/100000/1/1000000000                     154 ms          154 ms            4
BM_percentile_disc_flatten_and_sort/100/100000/1/1000000000                    327 ms          327 ms            2
BM_percentile_disc_flatten_and_sort/10/1000000/1/1000000000                    306 ms          306 ms            2
BM_percentile_disc_flatten_and_sort/50/1000000/1/1000000000                   1653 ms         1653 ms            1
BM_percentile_disc_flatten_and_sort/100/1000000/1/1000000000                  3756 ms         3754 ms            1
BM_percentile_disc_flatten_and_std_nth_element/10/10000/1/1000000000         0.184 ms        0.184 ms         3816
BM_percentile_disc_flatten_and_std_nth_element/50/10000/1/1000000000          1.14 ms         1.14 ms          651
BM_percentile_disc_flatten_and_std_nth_element/100/10000/1/1000000000         1.95 ms         1.95 ms          351
BM_percentile_disc_flatten_and_std_nth_element/10/100000/1/1000000000         2.11 ms         2.11 ms          328
BM_percentile_disc_flatten_and_std_nth_element/50/100000/1/1000000000         19.4 ms         19.4 ms           36
BM_percentile_disc_flatten_and_std_nth_element/100/100000/1/1000000000        49.5 ms         49.5 ms           13
BM_percentile_disc_flatten_and_std_nth_element/10/1000000/1/1000000000        59.5 ms         59.5 ms           13
BM_percentile_disc_flatten_and_std_nth_element/50/1000000/1/1000000000         267 ms          267 ms            3
BM_percentile_disc_flatten_and_std_nth_element/100/1000000/1/1000000000        472 ms          471 ms            2
BM_percentile_disc_nth_element/10/10000/1/1000000000                         0.009 ms        0.009 ms        74052
BM_percentile_disc_nth_element/50/10000/1/1000000000                         0.037 ms        0.037 ms        19204
BM_percentile_disc_nth_element/100/10000/1/1000000000                        0.079 ms        0.079 ms         9769
BM_percentile_disc_nth_element/10/100000/1/1000000000                        0.017 ms        0.017 ms        43883
BM_percentile_disc_nth_element/50/100000/1/1000000000                        0.070 ms        0.070 ms         8824
BM_percentile_disc_nth_element/100/100000/1/1000000000                       0.194 ms        0.193 ms         4494
BM_percentile_disc_nth_element/10/1000000/1/1000000000                       0.031 ms        0.031 ms        22990
BM_percentile_disc_nth_element/50/1000000/1/1000000000                       0.187 ms        0.187 ms         4015
BM_percentile_disc_nth_element/100/1000000/1/1000000000                      0.389 ms        0.388 ms         1963
BM_percentile_disc_nth_element/10/10000000/1/1000000000                      0.072 ms        0.071 ms        11883
BM_percentile_disc_nth_element/50/10000000/1/1000000000                      0.314 ms        0.314 ms         2204
BM_percentile_disc_nth_element/100/10000000/1/1000000000                     0.656 ms        0.655 ms          963
*/

namespace {
enum class percentile_disc_benchmark_type { NTH_ELEMENT, FLATTEN_AND_SORT, FLATTEN_AND_STD_NTH_ELEMENT };

auto generate_inner_vector(const std::size_t num_inner, std::mt19937& generator,
                           std::uniform_int_distribution<>& distribution) {
    std::vector<int> inner_vector;
    inner_vector.reserve(num_inner);

    for (int j = 0; j < num_inner; ++j) {
        auto num = distribution(generator);
        inner_vector.push_back(num);
    }

    std::ranges::sort(inner_vector.begin(), inner_vector.end());

    return inner_vector;
}

auto generate_inner_vector(const std::size_t num_inner, const int min_value, const int max_value) {
    std::mt19937 generator(0);
    std::uniform_int_distribution distribution(min_value, max_value);

    return generate_inner_vector(num_inner, generator, distribution);
}

auto generate_sorted_nested_vector(const std::size_t num_outer, const std::size_t num_inner, const int min_value,
                                   const int max_value) {
    std::mt19937 generator(0);
    std::uniform_int_distribution distribution(min_value, max_value);
    std::vector<std::vector<int>> result;
    result.reserve(num_outer);

    for (int i = 0; i < num_outer; ++i) {
        auto inner_vector = generate_inner_vector(num_inner, generator, distribution);
        result.push_back(std::move(inner_vector));
    }

    return result;
}

auto flatten_and_sort(const std::vector<std::vector<int>>& grid, const std::vector<int>& local_vec, const double rate) {
    std::vector<int> new_vector = local_vec;
    for (auto& innerData : grid) {
        std::move(innerData.begin() + 1, innerData.end() - 1, std::back_inserter(new_vector));
    }

    pdqsort(new_vector.begin(), new_vector.end());

    std::size_t index = std::floor(new_vector.size() * rate);
    index = std::clamp(index, 0UL, new_vector.size() - 1);

    return new_vector[index];
}

auto flatten_and_std_nth_element(const std::vector<std::vector<int>>& grid, const std::vector<int>& local_vec,
                                 const double rate) {
    std::vector<int> new_vector = local_vec;
    for (auto& innerData : grid) {
        std::move(innerData.begin() + 1, innerData.end() - 1, std::back_inserter(new_vector));
    }

    std::size_t index = std::floor(new_vector.size() * rate);
    index = std::clamp(index, 0UL, new_vector.size() - 1);

    std::ranges::nth_element(new_vector, new_vector.begin() + index);

    return new_vector[index];
}

void do_bench(benchmark::State& state, const percentile_disc_benchmark_type bm_type) {
    const std::size_t num_outer = state.range(0);
    const std::size_t num_inner = state.range(1);
    const int min_value = state.range(2);
    const int max_value = state.range(3);
    auto grid = generate_sorted_nested_vector(num_outer, num_inner, min_value, max_value);
    const auto local_vector = generate_inner_vector(num_outer, min_value, max_value);
    auto grid_wrapper = [&grid, &local_vector]() {
        std::vector<std::span<const int>> grid_wrapper;
        grid_wrapper.reserve(grid.size() + 1);
        std::ranges::transform(grid, std::back_inserter(grid_wrapper), [](const auto& row) { return std::span(row); });
        grid_wrapper.emplace_back(local_vector);
        return grid_wrapper;
    }();

    for (auto _ : state) {
        const auto row_count = num_outer * num_inner;
        constexpr double rate = 0.5;

        switch (bm_type) {
        case percentile_disc_benchmark_type::FLATTEN_AND_SORT: {
            flatten_and_sort(grid, local_vector, rate);
            break;
        }
        case percentile_disc_benchmark_type::FLATTEN_AND_STD_NTH_ELEMENT: {
            flatten_and_std_nth_element(grid, local_vector, rate);
            break;
        }
        case percentile_disc_benchmark_type::NTH_ELEMENT: {
            using CppType = RunTimeCppType<TYPE_INT>;

            std::size_t n = std::floor(row_count * rate);
            n = std::clamp(n, 0UL, row_count - 1);
            details::calculate_nth_element<CppType, CppType>(grid_wrapper, n);
            break;
        }
        default:
            break;
        }
    }
}

void BM_percentile_disc_flatten_and_sort(benchmark::State& state) {
    do_bench(state, percentile_disc_benchmark_type::FLATTEN_AND_SORT);
}

void BM_percentile_disc_flatten_and_std_nth_element(benchmark::State& state) {
    do_bench(state, percentile_disc_benchmark_type::FLATTEN_AND_STD_NTH_ELEMENT);
}

void BM_percentile_disc_nth_element(benchmark::State& state) {
    do_bench(state, percentile_disc_benchmark_type::NTH_ELEMENT);
}
} // namespace

// Arguments: number of grid rows, number of grid columns, minimum value, maximum value
BENCHMARK(BM_percentile_disc_flatten_and_sort)
        // Skip 10'000'000 grid columns for this BM as it's very memory intensive
        ->ArgsProduct({{10, 50, 100}, {10'000, 100'000, 1'000'000}, {1}, {1'000'000'000}})
        ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_percentile_disc_flatten_and_std_nth_element)
        // Skip 10'000'000 grid columns for this BM as it's very memory intensive
        ->ArgsProduct({{10, 50, 100}, {10'000, 100'000, 1'000'000}, {1}, {1'000'000'000}})
        ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_percentile_disc_nth_element)
        ->ArgsProduct({{10, 50, 100}, {10'000, 100'000, 1'000'000, 10'000'000}, {1}, {1'000'000'000}})
        ->Unit(benchmark::kMillisecond);

} // namespace starrocks

BENCHMARK_MAIN();