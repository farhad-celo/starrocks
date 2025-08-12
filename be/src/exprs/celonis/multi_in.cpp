#include "exprs/celonis/multi_in.h"

#include "column/array_column.h"
#include "column/struct_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

// Visitor to extract integer value
struct IntExtractor {
    template<typename T>
    std::optional<int64_t> operator()(const T& value) const {
        if constexpr (std::is_integral_v<T>) {
            return static_cast<int64_t>(value);
        } else {
            return std::nullopt;
        }
    }
};

// Visitor to extract floating-point value
struct FloatExtractor {
    template<typename T>
    std::optional<double> operator()(const T& value) const {
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<double>(value);
        } else {
            return std::nullopt;
        }
    }
};

bool equal(const DatumKey& var1, const DatumKey& var2) {
    auto int1 = std::visit(IntExtractor{}, var1);
    auto int2 = std::visit(IntExtractor{}, var2);

    auto float1 = std::visit(FloatExtractor{}, var1);
    auto float2 = std::visit(FloatExtractor{}, var2);

    if (int1 && float2) {
        return static_cast<double>(*int1) == *float2;
    } else if (int2 && float1) {
        return static_cast<double>(*int2) == *float1;
    }
    return var1 == var2;
}

}

StatusOr<ColumnPtr>
CelonisMultiIn::multi_in([[maybe_unused]] starrocks::FunctionContext* context,
                         const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    DCHECK(columns[0]->is_struct());
    DCHECK(columns[1]->is_struct());
    auto& input_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto& match_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[1].get()))->fields();
    const auto n_fields = input_fields.size();
    const size_t n_rows = columns[0]->size();
    ColumnBuilder<TYPE_BOOLEAN> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        if (match_fields.size() != n_fields) {
            result.append(false);
            continue;
        }
        std::optional<size_t> n_tuples;
        bool tuple_len_mismatch = false;
        for (auto j = 0; j < n_fields; ++j) {
            auto size = match_fields[j]->get(row).get_array().size();
            if (!n_tuples.has_value()) {
                n_tuples = size;
            } else {
                if (n_tuples.value() != size) {
                    tuple_len_mismatch = true;
                    break;
                }
            }
        }
        if (tuple_len_mismatch) {
            result.append(false);
            continue;
        }
        std::vector<DatumKey> keys;
        for (auto j = 0; j < n_fields; ++j) {
            keys.push_back(input_fields[j]->get(row).convert2DatumKey());
        }
        // traverse each tuple
        bool found_match = false;
        for (auto i = 0; i < n_tuples; ++i) {
            bool match = true;
            // check each field
            for (auto j = 0; j < n_fields; ++j) {
                auto key = match_fields[j]->get(row).get_array()[i].convert2DatumKey();
                if (!equal(keys[j], key)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                found_match = true;
                break;
            }
        }
        result.append(found_match);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
