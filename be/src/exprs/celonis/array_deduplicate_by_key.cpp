#include "exprs/celonis/array_deduplicate_by_key.h"

#include <nlohmann/json.hpp>
#include <span>

#include "column/array_column.h"
#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/hash_set.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

template <LogicalType LT>
class ArrayColumnHelper {
public:
    explicit ArrayColumnHelper(ColumnPtr column) : column_(std::move(column)) {
        const auto data = prepare_array_input(column_.get());

        this->elements_ = down_cast<const RunTimeColumnType<LT>&>(*data.elements).get_data().data();
        this->offsets_ = data.offsets->get_data().data();
        this->null_elements_ = data.null_elements;
    }

    [[nodiscard]] auto clone_empty() const { return column_->clone_empty(); }

    [[nodiscard]] bool is_null(const std::size_t row) const { return column_->is_null(row); }

    [[nodiscard]] bool is_null_element(const std::size_t row, const std::size_t index) const {
        if (null_elements_ == nullptr) {
            return false;
        }

        const auto start = offsets_[row];
        return null_elements_->at(start + index) != 0;
    }

    [[nodiscard]] std::span<const RunTimeCppType<LT>> array_for(const std::size_t row) const {
        const auto start = offsets_[row];
        const auto end = offsets_[row + 1];

        return std::span(elements_ + start, end - start);
    }

private:
    // Own the pointer to keep the column alive
    ColumnPtr column_{};

    const RunTimeCppType<LT>* elements_{};
    const unsigned* offsets_{};
    const NullColumn::Container* null_elements_{};
};

} // namespace

template <LogicalType KeyType, LogicalType ValueType>
StatusOr<ColumnPtr> CelonisArrayDeduplicateByKey<KeyType, ValueType>::array_deduplicate_by_key(FunctionContext* context,
                                                                                               const Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    const auto key_column =
            ArrayColumnHelper<KeyType>{ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[0])};
    const auto value_column =
            ArrayColumnHelper<ValueType>{ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[1])};

    ColumnPtr output_column = NullableColumn::wrap_if_necessary(value_column.clone_empty());

    using HashSet = std::conditional_t<std::is_same_v<RunTimeCppType<KeyType>, Slice>, SliceHashSet,
                                       std::unordered_set<RunTimeCppType<KeyType>>>;
    HashSet seen_keys_per_row{};
    for (std::size_t row = 0; row < num_rows; ++row) {
        if (key_column.is_null(row) || value_column.is_null(row)) {
            output_column->append_nulls(1);
            continue;
        }

        const auto keys = key_column.array_for(row);
        const auto values = value_column.array_for(row);

        if (keys.size() != values.size()) {
            const auto error = strings::Substitute(
                    "Invalid deduplicate by key: expected arrays of equal size but found key array of size [$0] and "
                    "value array of size [$1].",
                    keys.size(), values.size());
            context->set_error(error.c_str());
            return Status::InvalidArgument(error);
        }

        seen_keys_per_row.clear();
        auto row_result = DatumArray{};
        row_result.reserve(values.size());

        for (std::size_t idx = 0; idx < keys.size(); ++idx) {
            if (key_column.is_null_element(row, idx)) {
                // Null keys are skipped
                continue;
            }

            const auto key = keys[idx];
            const auto value = value_column.is_null_element(row, idx) ? Datum{} : Datum{values[idx]};

            auto [it, inserted] = seen_keys_per_row.insert(key);
            if (!inserted) {
                // Key has already been seen in this row; skip duplicate
                continue;
            }

            row_result.push_back(value);
        }

        output_column->append_datum(row_result);
    }

    if (all_const) {
        return ConstColumn::create(ColumnHelper::get_data_column(output_column.get())->clone(), num_rows);
    }
    return output_column;
}

// We provide explicit template instantiations for the types corresponding to PQL's data types.
template class CelonisArrayDeduplicateByKey<TYPE_BIGINT, TYPE_BIGINT>;
template class CelonisArrayDeduplicateByKey<TYPE_BIGINT, TYPE_DOUBLE>;
template class CelonisArrayDeduplicateByKey<TYPE_BIGINT, TYPE_VARCHAR>;
template class CelonisArrayDeduplicateByKey<TYPE_BIGINT, TYPE_DATETIME>;

template class CelonisArrayDeduplicateByKey<TYPE_DOUBLE, TYPE_BIGINT>;
template class CelonisArrayDeduplicateByKey<TYPE_DOUBLE, TYPE_DOUBLE>;
template class CelonisArrayDeduplicateByKey<TYPE_DOUBLE, TYPE_VARCHAR>;
template class CelonisArrayDeduplicateByKey<TYPE_DOUBLE, TYPE_DATETIME>;

template class CelonisArrayDeduplicateByKey<TYPE_VARCHAR, TYPE_BIGINT>;
template class CelonisArrayDeduplicateByKey<TYPE_VARCHAR, TYPE_DOUBLE>;
template class CelonisArrayDeduplicateByKey<TYPE_VARCHAR, TYPE_VARCHAR>;
template class CelonisArrayDeduplicateByKey<TYPE_VARCHAR, TYPE_DATETIME>;

template class CelonisArrayDeduplicateByKey<TYPE_DATETIME, TYPE_BIGINT>;
template class CelonisArrayDeduplicateByKey<TYPE_DATETIME, TYPE_DOUBLE>;
template class CelonisArrayDeduplicateByKey<TYPE_DATETIME, TYPE_VARCHAR>;
template class CelonisArrayDeduplicateByKey<TYPE_DATETIME, TYPE_DATETIME>;

} // namespace starrocks
