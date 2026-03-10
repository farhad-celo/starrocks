#pragma once

#include <random>
#include <span>

#include "exprs/agg/percentile_cont.h"

namespace starrocks {

namespace details {
template <typename CppType, LogicalType LT>
static std::vector<std::span<const CppType>> generate_grid_wrapper(
        const typename PercentileStateTypes<LT>::GridType& grid,
        const typename PercentileStateTypes<LT>::ItemType& local_vec) {
    std::vector<std::span<const CppType>> grid_wrapper;
    grid_wrapper.reserve(grid.size() + 1);
    for (const auto& row : grid) {
        if (row.size() < 2) {
            throw std::runtime_error(fmt::format("generate_grid_wrapper: Unexpected row size ({})", row.size()));
        }
        if (row.size() == 2) {
            continue;
        }
        grid_wrapper.emplace_back(std::span(row).subspan(1, row.size() - 2));
    }
    if (!local_vec.empty()) {
        grid_wrapper.emplace_back(local_vec);
    }
    return grid_wrapper;
}

// Input: Several pre-sorted partitions and a target index N.
// 1. Choose a suitable pivot element V.
// 2. Iterate through all partitions to determine the total count of elements across all partitions that are:
//    - Less than V (referred to as L)
//    - Equal to V (referred to as E)
//    - Greater than V (referred to as G)
// 3. Partitioning:
//    - If L <= N < L + E, the N-th element is V. The search is complete; return V.
//    - If L > N, the N-th element resides in the "left" subdivision (elements < V) of each partition.
//      Repeat the process focusing only on these subsets.
//    - If N >= L + E, the N-th element resides in the "right" subdivision (elements > V).
//      Update the target index to N - (L + E) and repeat the process on these subsets.
// 4. Select a new pivot V from the remaining active elements and return to Step 2.
template <typename InputCppType, typename ResultCppType>
ResultCppType calculate_nth_element(const std::vector<std::span<const InputCppType>>& grid_wrapper, std::size_t n) {
    using span_iter_t = std::span<const InputCppType>::iterator;
    using range_vector_t = std::vector<std::pair<span_iter_t, span_iter_t>>;

    range_vector_t search_windows;
    search_windows.reserve(grid_wrapper.size());
    std::ranges::transform(grid_wrapper, std::back_inserter(search_windows),
                           [](const auto& row) { return std::make_pair(row.begin(), row.end()); });
    std::random_device rd;
    std::mt19937 generator(rd());
    range_vector_t equal_pivot_ranges;
    equal_pivot_ranges.reserve(grid_wrapper.size());

    while (true) {
        std::uniform_int_distribution<std::size_t> search_windows_size_distribution{0, search_windows.size() - 1};
        const auto search_windows_pivot_index = search_windows_size_distribution(generator);
        const auto current_row_len = std::distance(search_windows.at(search_windows_pivot_index).first,
                                                   search_windows.at(search_windows_pivot_index).second);
        assert(current_row_len > 0);
        std::uniform_int_distribution<std::size_t> current_row_len_distribution{
                0, static_cast<size_t>(current_row_len - 1)};
        const auto current_row_pivot_index = current_row_len_distribution(generator);
        const auto current_row_pivot_element =
                *(search_windows.at(search_windows_pivot_index).first + current_row_pivot_index);
        auto global_before_pivot_count = 0UL;
        auto global_pivot_count = 0UL;
        bool has_empty_range = false;

        for (auto i = 0; i < search_windows.size(); ++i) {
            const auto [equal_range_begin, equal_range_end] = std::equal_range(
                    search_windows.at(i).first, search_windows.at(i).second, current_row_pivot_element);
            equal_pivot_ranges.emplace_back(equal_range_begin, equal_range_end);
            const auto local_pivot_count = std::distance(equal_range_begin, equal_range_end);
            const auto local_before_pivot_count = std::distance(search_windows.at(i).first, equal_range_begin);
            global_before_pivot_count += local_before_pivot_count;
            global_pivot_count += local_pivot_count;
        }
        if (global_before_pivot_count <= n && n < global_before_pivot_count + global_pivot_count) {
            return current_row_pivot_element;
        }
        if (n < global_before_pivot_count) {
            for (auto i = 0UL; i < search_windows.size(); ++i) {
                has_empty_range |= std::distance(search_windows.at(i).first, equal_pivot_ranges.at(i).first) == 0;
                search_windows.at(i) = std::make_pair(search_windows.at(i).first, equal_pivot_ranges.at(i).first);
            }
        } else {
            for (auto i = 0UL; i < search_windows.size(); ++i) {
                has_empty_range |= std::distance(equal_pivot_ranges.at(i).second, search_windows.at(i).second) == 0;
                search_windows.at(i) = std::make_pair(equal_pivot_ranges.at(i).second, search_windows.at(i).second);
            }
            n -= (global_before_pivot_count + global_pivot_count);
        }
        if (has_empty_range) {
            std::erase_if(search_windows,
                          [](const auto& iter_pair) { return std::distance(iter_pair.first, iter_pair.second) == 0; });
        }
        equal_pivot_ranges.clear();
    }
}
} // namespace details

// Customized implementation of PERCENTILE_DISC.
// SR's PERCENTILE_DISC uses index = ceil((group_size - 1) * rate);
// Saola's PERCENTILE uses index = floor(group_size * quantile_val); index = max(0, min(index, group_size - 1));
template <LogicalType LT>
class CelonisPercentileDiscAggregateFunction final : public PercentileContDiscAggregateFunction<LT> {
    using InputCppType = RunTimeCppType<LT>;
    using InputColumnType = RunTimeColumnType<LT>;
    static constexpr auto ResultLT = PercentileResultLT<LT, false>;
    using ResultCppType = RunTimeCppType<ResultLT>;
    using ResultColumnType = RunTimeColumnType<ResultLT>;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        typename PercentileStateTypes<LT>::ItemType local_vec{this->data(state).items};
        pdqsort(local_vec.begin(), local_vec.end());
        const auto grid_wrapper = details::generate_grid_wrapper<InputCppType, LT>(this->data(state).grid, local_vec);
        const auto rate = this->data(state).rate;
        ResultColumnType* column = down_cast<ResultColumnType*>(to);
        auto row_count = 0UL;

        for (auto i = 0UL; i < grid_wrapper.size(); i++) {
            row_count += grid_wrapper.at(i).size();
        }
        if (row_count == 0) {
            column->append_default();
            return;
        }

        std::size_t n = std::floor(row_count * rate);
        n = std::clamp(n, 0UL, row_count - 1);
        auto raw_result = details::calculate_nth_element<InputCppType, ResultCppType>(grid_wrapper, n);
        [[maybe_unused]] ResultCppType result;

        if constexpr (lt_is_datetime<LT>) {
            result.from_unix_second(raw_result.to_unix_second());
        } else if constexpr (lt_is_date<LT>) {
            result._julian = raw_result._julian;
        } else if constexpr (lt_is_arithmetic<LT> || lt_is_string<LT> || lt_is_decimal_of_any_version<LT>) {
            result = raw_result;
        } else {
            // won't go there if celonis_percentile_disc is registered correctly
            throw std::runtime_error("Invalid PrimitiveTypes for celonis_percentile_disc function");
        }

        column->append(result);
    }

    std::string get_name() const override { return "celonis_percentile_disc"; }
};

} // namespace starrocks
