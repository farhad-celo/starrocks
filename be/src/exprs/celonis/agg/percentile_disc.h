#pragma once

#include <execution>
#include <numeric>

#include "exprs/agg/percentile_cont.h"

namespace starrocks {


// Customized implementation of PERCENTILE_DISC.
// SR's PERCENTILE_DISC uses index = ceil((group_size - 1) * rate);
// Saola's PERCENTILE uses index = floor(group_size * quantile_val); index = max(0, min(index, group_size - 1));
template <LogicalType LT>
class CelonisPercentileDiscAggregateFunction final : public PercentileContDiscAggregateFunction<LT> {
    using InputCppType = RunTimeCppType<LT>;
    using InputColumnType = RunTimeColumnType<LT>;
    static constexpr auto ResultLT = PercentileResultLT<LT, false>;
    using ResultType = RunTimeCppType<ResultLT>;
    using ResultColumnType = RunTimeColumnType<ResultLT>;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override {
        auto new_vector = std::move(this->data(state).items);
        for (auto& innerData : this->data(state).grid) {
            std::move(innerData.begin() + 1, innerData.end() - 1, std::back_inserter(new_vector));
        }

        pdqsort(new_vector.begin(), new_vector.end());
        const double& rate = this->data(state).rate;

        ResultColumnType* column = down_cast<ResultColumnType*>(to);
        if (new_vector.empty()) {
            column->append_default();
            return;
        }
        if (new_vector.size() == 1 || rate == 1) {
            column->append(new_vector.back());
            return;
        }

        // Saola's implementation of index
        int index = std::floor(new_vector.size() * rate);
        index = std::max(0, std::min(index, static_cast<int>(new_vector.size()) - 1));

        [[maybe_unused]] ResultType result;
        if constexpr (lt_is_datetime<LT>) {
            result.from_unix_second(new_vector[index].to_unix_second());
        } else if constexpr (lt_is_date<LT>) {
            result._julian = new_vector[index]._julian;
        } else if constexpr (lt_is_arithmetic<LT> || lt_is_string<LT> || lt_is_decimal_of_any_version<LT>) {
            result = new_vector[index];
        } else {
            // won't go there if celonis_percentile_disc is registered correctly
            throw std::runtime_error("Invalid PrimitiveTypes for celonis_percentile_disc function");
        }

        column->append(result);
    }

    std::string get_name() const override { return "celonis_percentile_disc"; }
};

} // namespace starrocks
