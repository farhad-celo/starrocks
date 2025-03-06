#include "exprs/celonis/array_functions.h"

#include <vector>
#include <utility>

#include "column/array_column.h"
#include "exprs/celonis/util.h"
#include "column/column_builder.h"
#include "column/column_viewer.h"
#include "gutil/strings/strcat.h"

namespace starrocks {

class CelonisMergeSortedArrays {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK(columns.size() == 4 || columns.size() == 5 || columns.size() == 6);
        if (columns[0]->only_null()) {
            return Status::InvalidArgument("input_array column should not be NULL literal.");
        }
        if (columns[1]->only_null()) {
            return Status::InvalidArgument("timestamp_array column should not be NULL literal.");
        }
        if (columns[2]->only_null()) {
            return Status::InvalidArgument("size_array column should not be NULL literal.");
        }
        size_t chunk_size = columns[0]->size();
        ColumnPtr timestamp_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[1]);
        if (timestamp_column->has_null()) {
            return Status::InvalidArgument("timestamp_array should not be NULL.");
        }
        UnnestedArrayData timestamp_array_data = prepare_array_input(timestamp_column.get());
        DCHECK(timestamp_array_data.elements->is_timestamp());
        const auto& timestamp_offsets = timestamp_array_data.offsets->get_data().data();

        const bool has_secondary_order = (columns.size() >= 5) && (!columns[4]->has_null());
        ColumnPtr secondary_order_column = has_secondary_order ? ColumnHelper::unpack_and_duplicate_const_column(
                chunk_size, columns[4]) : nullptr;
        UnnestedArrayData secondary_order_array_data;
        const Column* sorting_keys = nullptr;
        const NullColumn::Container* null_sorting_keys = nullptr;
        if (has_secondary_order) {
            secondary_order_array_data = prepare_array_input(secondary_order_column.get());
            if (timestamp_array_data.offsets->get_data() != secondary_order_array_data.offsets->get_data()) {
                return Status::InvalidArgument(
                        "If provided, the size of secondary_order_array and timestamp_array should not be different.");
            }
            sorting_keys = secondary_order_array_data.elements;
            null_sorting_keys = secondary_order_array_data.null_elements;
        }

        ColumnPtr size_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[2]);
        if (size_column->has_null()) {
            return Status::InvalidArgument("size_array should not be NULL.");
        }
        UnnestedArrayData size_array_data = prepare_array_input(size_column.get());
        if (size_array_data.null_elements != nullptr) {
            return Status::InvalidArgument("size_array should not have NULL elements.");
        }
        const auto& sizes = down_cast<const RunTimeColumnType<TYPE_INT>&>(*size_array_data.elements).get_data().data();
        const auto& size_offsets = size_array_data.offsets->get_data().data();

        const bool has_priority = (columns.size() != 6) || (columns.size() == 6 && !columns[3]->has_null());
        ColumnPtr priority_column = (has_priority ? ColumnHelper::unpack_and_duplicate_const_column(chunk_size,
                                                                                                    columns[3])
                                                  : nullptr);
        const int32_t* priorities = nullptr;
        const bool similar_to_size = columns.size() != 6;
        if (has_priority) {
            if (priority_column->has_null()) {
                return Status::InvalidArgument("priority_array should not be NULL.");
            }
            UnnestedArrayData priority_array_data = prepare_array_input(priority_column.get());
            priorities = down_cast<const RunTimeColumnType<TYPE_INT>&>(*priority_array_data.elements).get_data().data();
            if (priority_array_data.null_elements != nullptr) {
                return Status::InvalidArgument("priority_array should not have NULL elements.");
            }
            const auto& priority_offsets = priority_array_data.offsets->get_data().data();
            for (auto row = 0; row < chunk_size; ++row) {
                const auto start = similar_to_size ? size_offsets[row] : timestamp_offsets[row];
                const auto end = similar_to_size ? size_offsets[row + 1] : timestamp_offsets[row + 1];
                if (priority_offsets[row] != start || priority_offsets[row + 1] != end) {
                    const auto priority_length = priority_offsets[row + 1] - priority_offsets[row];
                    const auto expected_length = end - start;
                    if (similar_to_size) {
                        return Status::InvalidArgument(
                                StrCat("If provided, priority_array (length = ", priority_length,
                                       ") should have the same length as size_array (length = ", expected_length,
                                       ").").c_str());
                    } else {
                        return Status::InvalidArgument(
                                StrCat("When limit is set and priority_array is not NULL literal, priority_array (length = ",
                                       priority_length, ") should have the same length as timestamp_array (length = ",
                                       expected_length, ").").c_str());
                    }
                }
            }
        }

        ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        auto* src_data_column = src_column.get();

        ColumnPtr dest_column = src_column->clone_empty();
        auto* dest_data_column = dest_column.get();

        if (src_column->is_nullable()) {
            if (src_column->has_null()) {
                return Status::InvalidArgument("input_array should not be null.");
            }
            const auto* src_nullable_column = down_cast<const NullableColumn*>(src_column.get());
            src_data_column = src_nullable_column->data_column().get();

            auto* dest_nullable_column = down_cast<NullableColumn*>(dest_column.get());
            dest_data_column = dest_nullable_column->mutable_data_column();
            auto* dest_null_column = dest_nullable_column->mutable_null_column();
            dest_null_column->get_data().resize(chunk_size, 0);
            dest_nullable_column->set_has_null(false);
        }

        const auto& src_elements = down_cast<const ArrayColumn*>(src_data_column)->elements();
        const auto& src_offsets = down_cast<const ArrayColumn*>(src_data_column)->offsets().get_data().data();
        auto* dest_elements_column = down_cast<ArrayColumn*>(dest_data_column)->elements_column().get();
        auto* dest_offsets_column = down_cast<ArrayColumn*>(dest_data_column)->offsets_column().get();

        std::vector<uint32_t> src_index;
        src_index.reserve(src_elements.size());
        int new_offset = 0;
        for (size_t row = 0; row < chunk_size; row++) {
            int64_t limit = INT64_MAX;
            if (columns.size() == 6 && !columns[5]->is_null(row)) {
                limit = columns[5]->get(row).get_int64();
                if (limit < 0) {
                    return Status::InvalidArgument("limit must not be negative.");
                }
            }
            size_t src_timestamp_start = src_offsets[row];
            size_t src_timestamp_end = src_offsets[row + 1];
            if (timestamp_offsets[row + 1] != src_timestamp_end) {
                return Status::InvalidArgument("The size of input_array and timestamp_array should not be different.");
            }
            size_t size_start = size_offsets[row];
            size_t size_end = size_offsets[row + 1];
            size_t total_size = 0;
            for (size_t i = size_start; i < size_end; ++i) {
                total_size += sizes[i];
            }
            if (total_size != timestamp_offsets[row + 1] - timestamp_offsets[row]) {
                return Status::InvalidArgument(
                        "The size of input_array and timestamp_array should not be different than the sum of "
                        "size_array.");
            }
            // based on benchmark, using unix_seconds is faster than using TimestampValue.
            std::vector<int64_t> unix_seconds;
            unix_seconds.reserve(total_size);
            auto timestamp_array = timestamp_column->get(row).get_array();
            for (const auto& item: timestamp_array) {
                if (item.is_null()) {
                    unix_seconds.push_back(std::numeric_limits<int64_t>::min());
                } else {
                    auto x = item.get_timestamp().to_unix_second();
                    unix_seconds.push_back(x);
                }
            }

            struct Array {
                Array(size_t start, size_t end, const int64_t* timestamp, size_t secondary_order_index,
                      const int32_t* priority)
                        : index(start), end(end), timestamp(timestamp), secondary_order_index(secondary_order_index),
                          priority(priority) {}

                size_t index;
                size_t end;
                const int64_t* timestamp;
                size_t secondary_order_index;
                const int32_t* priority;
            };
            struct CompareArrayElement {
                const Column* sorting_keys;
                const NullColumn::Container* null_sorting_keys;

                explicit CompareArrayElement(const Column* sorting_keys, const NullColumn::Container* null_sorting_keys)
                        : sorting_keys(sorting_keys), null_sorting_keys(null_sorting_keys) {}

                bool operator()(const Array& lhs, const Array& rhs) {
                    if (*lhs.timestamp != *rhs.timestamp) {
                        return *lhs.timestamp > *rhs.timestamp;
                    }
                    if (lhs.secondary_order_index == -1 || rhs.secondary_order_index == -1) {
                        return lhs.priority == nullptr || (*lhs.priority < *rhs.priority);
                    }
                    DatumKey lhs_key = get_sorting_key(lhs.secondary_order_index);
                    DatumKey rhs_key = get_sorting_key(rhs.secondary_order_index);
                    if (lhs_key == rhs_key) {
                        return lhs.priority == nullptr || (*lhs.priority < *rhs.priority);
                    }
                    return lhs_key > rhs_key;
                }

                // The corresponding DatumKey of NULL is std::monostate which is less than any other DatumKey.
                DatumKey get_sorting_key(size_t index) {
                    if (null_sorting_keys != nullptr && (*null_sorting_keys)[index] != 0) {
                        return std::monostate();
                    }
                    return sorting_keys->get(index).convert2DatumKey();
                }
            };
            std::priority_queue<Array, std::vector<Array>, CompareArrayElement> pq(
                    (CompareArrayElement(sorting_keys, null_sorting_keys)));
            size_t start = src_timestamp_start;
            size_t next = 0;
            for (size_t i = size_start; i < size_end; i++) {
                next = start + sizes[i];
                if (start == next) {
                    // Skip empty arrays.
                    continue;
                }
                auto local_start = start - src_timestamp_start;
                pq.emplace(start, next, unix_seconds.data() + local_start, has_secondary_order ? start : -1,
                           (has_priority ? (similar_to_size ? (priorities + i) : (priorities + start)) : nullptr));
                start = next;
            }
            int64_t cnt = 0;
            while (!pq.empty() && cnt < limit) {
                Array curr = pq.top();
                pq.pop();
                src_index.push_back(curr.index);
                new_offset++;
                cnt++;
                if (++curr.index < curr.end) {
                    curr.timestamp++;
                    if (curr.priority != nullptr && !similar_to_size) {
                        curr.priority++;
                    }
                    if (curr.secondary_order_index != -1) {
                        ++curr.secondary_order_index;
                    }
                    pq.push(curr);
                }
            }
            dest_offsets_column->get_data().push_back(new_offset);
        }
        dest_elements_column->append_selective(src_elements, src_index);
        return dest_column;
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::merge_sorted_arrays([[maybe_unused]] FunctionContext* context,
                                                               const Columns& columns) {
    return CelonisMergeSortedArrays::process(columns);
}

class CelonisDedupSortedBy {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        RETURN_IF_COLUMNS_ONLY_NULL(columns);

        size_t chunk_size = columns[0]->size();

        ColumnPtr src_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr dest_column = src_column->clone_empty();
        ColumnPtr key_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[1]);

        if (src_column->is_nullable()) {
            const auto* src_nullable_column = down_cast<const NullableColumn*>(src_column.get());
            const auto& src_data_column = src_nullable_column->data_column_ref();
            const auto& src_null_column = src_nullable_column->null_column_ref();

            auto* dest_nullable_column = down_cast<NullableColumn*>(dest_column.get());
            auto* dest_data_column = dest_nullable_column->mutable_data_column();
            auto* dest_null_column = dest_nullable_column->mutable_null_column();
            if (src_column->has_null()) {
                dest_null_column->get_data().assign(src_null_column.get_data().begin(),
                                                    src_null_column.get_data().end());
            } else {
                dest_null_column->get_data().resize(chunk_size, 0);
            }
            dest_nullable_column->set_has_null(src_nullable_column->has_null());
            RETURN_IF_ERROR(_dedup_array_column(dest_data_column, src_data_column, key_column));
        } else {
            RETURN_IF_ERROR(_dedup_array_column(dest_column.get(), *src_column, key_column));
        }
        return dest_column;
    }

private:
    static Status _dedup_array_column(Column* dest_array_column, const Column& src_array_column,
                                      const ColumnPtr key_array_ptr) {
        ColumnPtr key_array_data = key_array_ptr;
        if (key_array_ptr->is_nullable()) { // Nullable(array(Nullable(element), offsets), null_map)
            key_array_data = down_cast<const NullableColumn*>(key_array_ptr.get())->data_column();
        }
        // key_array_data is of array(Nullable(element), offsets)

        const auto& key_element_column = down_cast<ArrayColumn*>(key_array_data.get())->elements();
        if (key_element_column.has_null()) {
            return Status::InvalidArgument("key_array should not have null elements.");
        }
        const auto& key_offsets = down_cast<ArrayColumn*>(key_array_data.get())->offsets().get_data().data();

        const auto& src_elements_column = down_cast<const ArrayColumn&>(src_array_column).elements();
        const auto& src_offsets = down_cast<const ArrayColumn&>(src_array_column).offsets().get_data().data();

        auto* dest_elements_column = down_cast<ArrayColumn*>(dest_array_column)->elements_column().get();
        auto* dest_offsets_column = down_cast<ArrayColumn*>(dest_array_column)->offsets_column().get();

        size_t chunk_size = src_array_column.size();
        std::vector<uint32_t> src_index;
        src_index.reserve(src_elements_column.size());
        int new_offset = 0;

        const auto& key_data_column = down_cast<const NullableColumn&>(key_element_column).data_column_ref();
        const auto& key_data = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(key_data_column).get_data();

        for (size_t i = 0; i < chunk_size; i++) {
            Slice prev;
            size_t start = src_offsets[i];
            size_t end = src_offsets[i + 1];
            if (end == start) {
                dest_offsets_column->get_data().push_back(new_offset);
                continue;
            }
            size_t key_start = key_offsets[i];
            size_t key_end = key_offsets[i + 1];
            // if input_array is not empty, key_array must have the same length.
            if (key_end - key_start != end - start) {
                return Status::InvalidArgument("The size of input_array and key_array should not be different.");
            }
            for (auto id = key_start; id < key_end; ++id) {
                if (id == key_start || prev != key_data[id]) {
                    src_index.push_back(id - key_start + start);
                    new_offset++;
                    prev = key_data[id];
                }
            }
            dest_offsets_column->get_data().push_back(new_offset);
        }
        dest_elements_column->append_selective(src_elements_column, src_index);
        return Status::OK();
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::dedup_sorted_by([[maybe_unused]] FunctionContext* context,
                                                           const Columns& columns) {
    return CelonisDedupSortedBy::process(columns);
}

class CelonisArrayLag {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        RETURN_IF_COLUMNS_ONLY_NULL(columns);

        size_t chunk_size = columns[0]->size();
        ColumnViewer offset_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);

        for (size_t i = 0; i < chunk_size; ++i) {
            if (offset_viewer.is_null(i)) {
                return Status::InvalidArgument("offset column must not contain null.");
            }
            if (offset_viewer.value(i) <= 0) {
                return Status::InvalidArgument("offset must be a positive integer.");
            }
        }

        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr output_column = input_column->clone_empty();

        if (input_column->is_nullable()) {
            const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
            const auto& input_data_column = input_nullable_column->data_column_ref();
            const auto& input_null_column = input_nullable_column->null_column_ref();

            auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
            auto* output_data_column = output_nullable_column->mutable_data_column();
            auto* output_null_column = output_nullable_column->mutable_null_column();
            if (input_column->has_null()) {
                output_null_column->get_data().assign(input_null_column.get_data().begin(),
                                                      input_null_column.get_data().end());
            } else {
                output_null_column->get_data().resize(chunk_size, 0);
            }
            output_nullable_column->set_has_null(input_nullable_column->has_null());
            RETURN_IF_ERROR(_array_lag(output_data_column, input_data_column, offset_viewer));
        } else {
            RETURN_IF_ERROR(_array_lag(output_column.get(), *input_column, offset_viewer));
        }
        return output_column;
    }

private:
    static Status _array_lag(Column* output_array_column, const Column& input_array_column,
                             const ColumnViewer<TYPE_BIGINT>& offset_viewer) {
        const auto& input_elements_column = down_cast<const ArrayColumn&>(input_array_column).elements();
        const auto& input_offsets = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data().data();

        auto* output_elements_column = down_cast<ArrayColumn*>(output_array_column)->elements_column().get();
        auto* output_offsets_column = down_cast<ArrayColumn*>(output_array_column)->offsets_column().get();
        output_elements_column->reserve(input_offsets[input_array_column.size()]);

        for (size_t i = 0; i < input_array_column.size(); i++) {
            size_t start = input_offsets[i];
            size_t end = input_offsets[i + 1];
            DCHECK(end >= start);
            int64_t lag_offset = offset_viewer.value(i);
            std::deque<size_t> window;
            for (size_t j = start; j < end; ++j) {
                if (window.size() == lag_offset) {
                    output_elements_column->append(input_elements_column, window.front(), 1);
                } else {
                    output_elements_column->append_nulls(1);
                }
                if (!input_elements_column.get(j).is_null()) {
                    window.push_back(j);
                }
                if (window.size() > lag_offset) {
                    window.pop_front();
                }
            }
        }
        output_offsets_column->get_data() = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data();
        return Status::OK();
    }
};


StatusOr<ColumnPtr> CelonisArrayFunctions::array_lag([[maybe_unused]] FunctionContext* context,
                                                     const Columns& columns) {
    return CelonisArrayLag::process(columns);
}

class CelonisArrayLead {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        DCHECK_EQ(columns.size(), 2);
        RETURN_IF_COLUMNS_ONLY_NULL(columns);

        size_t chunk_size = columns[0]->size();
        ColumnViewer offset_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);

        for (size_t i = 0; i < chunk_size; ++i) {
            if (offset_viewer.is_null(i)) {
                return Status::InvalidArgument("offset column must not contain null.");
            }
            if (offset_viewer.value(i) <= 0) {
                return Status::InvalidArgument("offset must be a positive integer.");
            }
        }

        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(chunk_size, columns[0]);
        ColumnPtr output_column = input_column->clone_empty();

        if (input_column->is_nullable()) {
            const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
            const auto& input_data_column = input_nullable_column->data_column_ref();
            const auto& input_null_column = input_nullable_column->null_column_ref();

            auto* output_nullable_column = down_cast<NullableColumn*>(output_column.get());
            auto* output_data_column = output_nullable_column->mutable_data_column();
            auto* output_null_column = output_nullable_column->mutable_null_column();
            if (input_column->has_null()) {
                output_null_column->get_data().assign(input_null_column.get_data().begin(),
                                                      input_null_column.get_data().end());
            } else {
                output_null_column->get_data().resize(chunk_size, 0);
            }
            output_nullable_column->set_has_null(input_nullable_column->has_null());
            RETURN_IF_ERROR(_array_lead(output_data_column, input_data_column, offset_viewer));
        } else {
            RETURN_IF_ERROR(_array_lead(output_column.get(), *input_column, offset_viewer));
        }
        return output_column;
    }

private:
    static Status _array_lead(Column* output_array_column, const Column& input_array_column,
                              const ColumnViewer<TYPE_BIGINT>& offset_viewer) {
        const auto& input_elements_column = down_cast<const ArrayColumn&>(input_array_column).elements();
        const auto& input_offsets = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data().data();

        auto* output_elements_column = down_cast<ArrayColumn*>(output_array_column)->elements_column().get();
        auto* output_offsets_column = down_cast<ArrayColumn*>(output_array_column)->offsets_column().get();
        output_elements_column->reserve(input_offsets[input_array_column.size()]);

        for (size_t i = 0; i < input_array_column.size(); i++) {
            size_t start = input_offsets[i];
            size_t end = input_offsets[i + 1];
            DCHECK(end >= start);
            int64_t lead_offset = offset_viewer.value(i);
            std::deque<size_t> window;
            std::vector<std::optional<size_t>> idxes;
            idxes.reserve(end - start);
            // traverse the elements reversely.
            for (size_t j = end; j-- > start;) {
                if (window.size() == lead_offset) {
                    idxes.emplace_back(window.front());
                } else {
                    idxes.emplace_back(std::nullopt);
                }
                if (!input_elements_column.get(j).is_null()) {
                    window.push_back(j);
                }
                if (window.size() > lead_offset) {
                    window.pop_front();
                }
            }
            for (auto it = idxes.rbegin(); it != idxes.rend(); ++it) {
                if (it->has_value()) {
                    output_elements_column->append(input_elements_column, it->value(), 1);
                } else {
                    output_elements_column->append_nulls(1);
                }
            }
        }
        output_offsets_column->get_data() = down_cast<const ArrayColumn&>(input_array_column).offsets().get_data();
        return Status::OK();
    }
};


StatusOr<ColumnPtr> CelonisArrayFunctions::array_lead([[maybe_unused]] FunctionContext* context,
                                                      const Columns& columns) {
    return CelonisArrayLead::process(columns);
}

class CelonisNullToEmpty {
public:
    static StatusOr<ColumnPtr> process(const Columns& columns) {
        if (!columns[0]->is_nullable() || !columns[0]->has_null()) {
            return columns[0];
        }
        size_t n_rows = columns[0]->size();
        ColumnPtr input_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);

        const auto* input_nullable_column = down_cast<const NullableColumn*>(input_column.get());
        const auto& input_data_column = input_nullable_column->data_column_ref();

        // It should return a non-nullable column because it is registered in celonisAlwaysReturnNonNullableFunctions.
        ColumnPtr output_column = input_data_column.clone_empty();

        for (size_t i = 0; i < n_rows; ++i) {
            if (input_column->is_null(i)) {
                output_column->append_default();
            } else {
                output_column->append(input_data_column, i, 1);
            }
        }
        return output_column;
    }
};

StatusOr<ColumnPtr> CelonisArrayFunctions::null_to_empty([[maybe_unused]] FunctionContext* context,
                                                         const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    if (columns[0]->only_null()) {
        auto result_column = context->create_column(context->get_return_type(), false);
        result_column->append_datum(DatumArray{});
        return ConstColumn::create(std::move(result_column), columns[0]->size());
    }
    return CelonisNullToEmpty::process(columns);
}

Status calc_crop_impl(const Columns& columns, bool fill_one, ColumnPtr result) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnPtr activity_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData activity_array_data = prepare_array_input(activity_array_column.get());
    DCHECK(activity_array_data.elements->is_binary());
    const auto& activities = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *activity_array_data.elements).get_data().data();
    const auto& activity_offsets = activity_array_data.offsets->get_data().data();

    ColumnViewer begin_activity_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer begin_mode_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer end_activity_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnViewer end_mode_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);

    DCHECK(result->is_nullable());
    auto* res_nullable_column = down_cast<NullableColumn*>(result.get());
    auto* res_null_column = res_nullable_column->mutable_null_column();
    auto* res_data_column = res_nullable_column->mutable_data_column();
    auto* res_elements_column = down_cast<ArrayColumn*>(res_data_column)->elements_column().get();
    auto* res_offsets_column = down_cast<ArrayColumn*>(res_data_column)->offsets_column().get();
    size_t new_offset = 0;
    res_elements_column->reserve(activity_offsets[n_rows]);

    // TODO(y.zhang): Add prepare method to handle constant parameters; handle AllAll specially.
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || begin_mode_viewer.is_null(row) ||
            (begin_mode_viewer.value(row).to_string() != "ALL" && begin_activity_viewer.is_null(row)) ||
            end_mode_viewer.is_null(row) ||
            (end_mode_viewer.value(row).to_string() != "ALL" && end_activity_viewer.is_null(row))) {
            result->append_nulls(1);
            continue;
        }
        res_null_column->get_data().push_back(0);
        const std::string begin_mode = begin_mode_viewer.value(row).to_string();
        const std::string end_mode = end_mode_viewer.value(row).to_string();
        if (begin_mode != "FIRST" && begin_mode != "LAST" && begin_mode != "ALL") {
            return Status::InvalidArgument("begin range mode must be FIRST/LAST/ALL.");
        }
        if (end_mode != "FIRST" && end_mode != "LAST" && end_mode != "ALL") {
            return Status::InvalidArgument("end range mode must be FIRST/LAST/ALL.");
        }
        const std::string begin_activity = begin_mode == "ALL" ? "" : begin_activity_viewer.value(row).to_string();
        const std::string end_activity = end_mode == "ALL" ? "" : end_activity_viewer.value(row).to_string();

        const auto start = static_cast<int64_t>(activity_offsets[row]);
        const auto end = static_cast<int64_t>(activity_offsets[row + 1]);
        DCHECK(end >= start);
        const auto size = end - start;
        std::optional<int64_t> first_begin_index = std::nullopt;
        std::optional<int64_t> last_begin_index = std::nullopt;
        std::optional<int64_t> first_end_index = std::nullopt;
        std::optional<int64_t> last_end_index = std::nullopt;
        for (int64_t i = start; i < end; ++i) {
            if (activity_array_data.null_elements != nullptr && (*activity_array_data.null_elements)[i] != 0) {
                continue;
            }
            const std::string activity = activities[i].to_string();
            if (activity == begin_activity) {
                last_begin_index = i;
                if (!first_begin_index.has_value()) {
                    first_begin_index = i;
                }
            }
            if (activity == end_activity) {
                last_end_index = i;
                if (!first_end_index.has_value()) {
                    first_end_index = i;
                }
            }
        }
        std::optional<int64_t> begin_index = std::nullopt;
        std::optional<int64_t> end_index = std::nullopt;
        if (begin_mode == "FIRST") {
            begin_index = first_begin_index;
        } else if (begin_mode == "LAST") {
            begin_index = last_begin_index;
        } else {
            DCHECK(begin_mode == "ALL");
            begin_index = start;
        }
        if (end_mode == "FIRST") {
            end_index = first_end_index;
        } else if (end_mode == "LAST") {
            end_index = last_end_index;
        } else {
            DCHECK(end_mode == "ALL");
            end_index = end - 1;
        }
        if (!begin_index.has_value() || !end_index.has_value() || begin_index.value() > end_index.value()) {
            for (int64_t j = 0; j < size; ++j) {
                res_elements_column->append_datum(kNullDatum);
            }
        } else {
            for (int64_t j = start; j < begin_index.value(); ++j) {
                res_elements_column->append_datum(kNullDatum);
            }
            for (int64_t j = begin_index.value(); j <= end_index.value(); ++j) {
                if (fill_one) {
                    res_elements_column->append_datum(Datum(1L));
                } else {
                    if (activity_array_data.null_elements != nullptr && (*activity_array_data.null_elements)[j] != 0) {
                        res_elements_column->append_datum(kNullDatum);
                    } else {
                        res_elements_column->append_datum(activities[j]);
                    }
                }
            }
            for (int64_t j = end_index.value() + 1; j < end; ++j) {
                res_elements_column->append_datum(kNullDatum);
            }
        }
        new_offset += size;
        res_offsets_column->get_data().push_back(new_offset);
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisArrayFunctions::calc_crop([[maybe_unused]] FunctionContext* context,
                                                     const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    TypeDescriptor type_array_bigint;
    type_array_bigint.type = TYPE_ARRAY;
    type_array_bigint.children.resize(1);
    type_array_bigint.children[0].type = TYPE_BIGINT;
    type_array_bigint.children[0].len = -1;
    auto result = ColumnHelper::create_column(type_array_bigint, true);
    RETURN_IF_ERROR(calc_crop_impl(columns, true, result));
    return result;
}

StatusOr<ColumnPtr> CelonisArrayFunctions::calc_crop_to_null([[maybe_unused]] FunctionContext* context,
                                                             const Columns& columns) {
    DCHECK(columns.size() == 5);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[0] });
    auto result = NullableColumn::wrap_if_necessary(columns[0]->clone_empty());
    RETURN_IF_ERROR(calc_crop_impl(columns, false, result));
    return result;
}

StatusOr<ColumnPtr> CelonisArrayFunctions::array_count([[maybe_unused]] FunctionContext* context,
                                                       const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const size_t n_rows = columns[0]->size();
    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    const auto& offsets = array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BIGINT> result(n_rows);

    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        if (array_data.null_elements == nullptr) {
            result.append(end - start);
            continue;
        }
        int64_t cnt = 0;
        for (auto i = start; i < end; ++i) {
            if ((*array_data.null_elements)[i] != 0) {
                continue;
            }
            ++cnt;
        }
        result.append(cnt);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisArrayFunctions::array_bool_or([[maybe_unused]] FunctionContext* context,
                                                         const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    const size_t n_rows = columns[0]->size();
    ColumnPtr boolean_array_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[0]);
    UnnestedArrayData boolean_array_data = prepare_array_input(boolean_array_column.get());
    const auto& booleans =
            down_cast<const RunTimeColumnType<TYPE_BOOLEAN>&>(*boolean_array_data.elements).get_data().data();
    const auto& offsets = boolean_array_data.offsets->get_data().data();
    ColumnBuilder<TYPE_BOOLEAN> result(n_rows);

    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        const auto start = offsets[row];
        const auto end = offsets[row + 1];
        bool is_true = false;
        for (auto i = start; i < end; ++i) {
            if (boolean_array_data.null_elements != nullptr && (*boolean_array_data.null_elements)[i] != 0) {
                continue;
            }
            if (booleans[i]) {
                is_true = true;
                break;
            }
        }
        result.append(is_true);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
