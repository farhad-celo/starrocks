#include "exprs/celonis/adjust_daily_timestamps.h"

#include "column/struct_column.h"
#include "exprs/celonis/reorder_single_day.h"
#include "exprs/celonis/util.h"

namespace starrocks {

namespace {

// Holds the initialized output column with accessor to all relevant data fields
struct DeconstructedOutputColumn {
    ColumnPtr final_output;
    Int64Column::Ptr output_reordering_elements;
    TimestampColumn::Ptr output_timestamp_elements;
    NullColumn::Ptr output_timestamp_null_flags;
};

// Holds a deconstructed version of the input columns with accessor to all relevant data fields
struct DeconstructedInput {
    UnnestedArrayData timestamp_data;
    UnnestedArrayData is_day_based_data;
    UnnestedArrayData sorting_data;
    size_t num_cases;
};

// Initializes all the output data and returns it in a way where the individual data fields can be easily accessed. All
// output columns will already have the correct size, so the respective arrays can be directly written.
DeconstructedOutputColumn create_output_column(const Column& input_timestamp_column) {
    // We use the input timestamp column as a template for the output
    UnnestedArrayData timestamp_array_data{prepare_array_input(&input_timestamp_column)};

    // The output timestamps are an exact copy of the input timestamps. We will only rewrite a small amount of rows.
    ColumnPtr output_timestamps{input_timestamp_column.clone_shared()};
    auto* output_timestamp_arrays{down_cast<ArrayColumn*>(ColumnHelper::get_data_column(output_timestamps.get()))};
    auto* output_timestamp_elements_nullable{
            down_cast<NullableColumn*>(output_timestamp_arrays->elements_column().get())};
    NullColumn::Ptr output_timestamp_elements_null_flags{std::move(output_timestamp_elements_nullable->null_column())};
    TimestampColumn::Ptr output_timestamp_elements{
            std::static_pointer_cast<TimestampColumn>(output_timestamp_elements_nullable->data_column())};

    // For the output reordering we base it on the input timestamps in terms of dimensions, but do not copy any data. All
    // null flags for the output reordering are 0 as there will be no null values. The array offsets are copied from the
    // timestamps, as columns should have same array sizes.
    Int64Column::Ptr output_reordering_elements_data{Int64Column::create(timestamp_array_data.elements->size())};
    NullColumn::Ptr output_reordering_elements_null_flags{
            NullColumn::create(output_reordering_elements_data->size(), 0)};
    NullableColumn::Ptr output_reordering_elements{
            NullableColumn::create(output_reordering_elements_data, std::move(output_reordering_elements_null_flags))};
    UInt32Column::Ptr output_reordering_offsets{
            std::static_pointer_cast<UInt32Column>(output_timestamp_arrays->offsets().clone_shared())};
    ColumnPtr output_reordering{ArrayColumn::create(output_reordering_elements, output_reordering_offsets)};

    // If the input timestamps are nullable then we also wrap the output reordering into a nullable column and copy the array
    // null flags from the timestamps.
    if (input_timestamp_column.is_nullable()) {
        auto* nullable_input{down_cast<const NullableColumn*>(&input_timestamp_column)};
        ColumnPtr array_null_flags{nullable_input->null_column()->clone_shared()};
        output_reordering = NullableColumn::create(std::move(output_reordering),
                                                   std::static_pointer_cast<NullColumn>(array_null_flags));
    }

    // Fill the reordering arrays for each case with iotas.
    for (size_t case_idx{0}; case_idx < output_reordering->size(); case_idx++) {
        const auto case_start_it{std::next(output_reordering_elements_data->get_data().begin(),
                                           output_reordering_offsets->get_data()[case_idx])};
        const auto case_end_it{std::next(output_reordering_elements_data->get_data().begin(),
                                         output_reordering_offsets->get_data()[case_idx + 1])};
        std::iota(case_start_it, case_end_it, int64_t{0});
    }

    // Create the final output column and return it in a deconstructed way.
    ColumnPtr output_column{StructColumn::create(Columns{output_timestamps, output_reordering},
                                                 std::vector<std::string>{"adjusted_timestamps", "reordering"})};
    return DeconstructedOutputColumn{.final_output = std::move(output_column),
                                     .output_reordering_elements = std::move(output_reordering_elements_data),
                                     .output_timestamp_elements = std::move(output_timestamp_elements),
                                     .output_timestamp_null_flags = std::move(output_timestamp_elements_null_flags)};
}

// Remaps a timestamps to a timestamp reduced to a day (i.e. all hours, minutes, seconds ... are zeroed).
TimestampValue extract_day(TimestampValue timestamp) {
    timestamp.trunc_to_day();
    return timestamp;
}

// Check if the hours, minutes and seconds of a timestamp are zeroed.
bool is_day_based_timestamp(const TimestampValue& timestamp) {
    return timestamp == extract_day(timestamp);
}

// Given the start idx and end idx of a single day, reorder that day and write the output data correspondingly.
void reorder_day(const size_t day_start_idx, const size_t day_end_idx, const size_t case_offset,
                 const DeconstructedInput& input, DeconstructedOutputColumn& output_data) {
    // Lambda for comparing the sorting column value of the activities in the two input rows.
    auto sorting_cmp{[&](const int64_t i, const int64_t j) -> bool {
        if (input.sorting_data.null_elements != nullptr) {
            if (input.sorting_data.null_elements->data()[i] && input.sorting_data.null_elements->data()[j]) {
                return i < j;
            } else if (input.sorting_data.null_elements->data()[i]) {
                return true;
            } else if (input.sorting_data.null_elements->data()[j]) {
                return false;
            }
        }

        const auto& data{down_cast<const Int64Column*>(input.sorting_data.elements)->get_data()};
        if (data[i] == data[j]) {
            return i < j;
        }
        return data[i] < data[j];
    }};

    // Lambda for rewriting timestamps. The input timestamp at index j is written to the output timestamps at index i.
    auto rewrite_timestamps{[&](const int64_t i, const int64_t j) -> void {
        if (input.timestamp_data.null_elements != nullptr && input.timestamp_data.null_elements->data()[j]) {
            output_data.output_timestamp_null_flags->get_data()[i] = 1;
            return;
        }
        const auto& data{down_cast<const TimestampColumn*>(input.timestamp_data.elements)->get_data()};
        output_data.output_timestamp_elements->get_data()[i] = data[j];
    }};

    // Lambda for checking whether activity in input row i is a day based activity.
    auto is_day_based{[&](const int64_t i) -> bool {
        if (input.is_day_based_data.null_elements != nullptr && input.is_day_based_data.null_elements->data()[i]) {
            return false;
        }
        const auto& data{down_cast<const BooleanColumn*>(input.is_day_based_data.elements)->get_data()};
        // Check that if day_based is set the timestamp ends on null
        DCHECK(!data[i] ||
               is_day_based_timestamp(down_cast<const TimestampColumn*>(input.timestamp_data.elements)->get_data()[i]));
        return data[i];
    }};

    // This will rewrite all timestamps, but we still need to reorder after.
    celonis::accelerator::cube::reorder_single_day(day_start_idx, day_end_idx, sorting_cmp, rewrite_timestamps,
                                                   is_day_based);

    // Compare function for the reorder. It compares two rows by first checking the (rewritten timestamps) and then
    // the sorting column values.
    auto reorder_cmp{[&sorting_cmp, &output_data](const int64_t i, const int64_t j) {
        if (output_data.output_timestamp_null_flags->get_data()[i] &&
            output_data.output_timestamp_null_flags->get_data()[j]) {
            return sorting_cmp(i, j);
        } else if (output_data.output_timestamp_null_flags->get_data()[i]) {
            return true;
        } else if (output_data.output_timestamp_null_flags->get_data()[j]) {
            return false;
        } else {
            const auto& data{output_data.output_timestamp_elements->get_data()};
            if (data[i] == data[j]) {
                return sorting_cmp(i, j);
            }
            return data[i] < data[j];
        }
    }};

    // Perform the actual reordering.
    auto& reordering_vec{output_data.output_reordering_elements->get_data()};
    auto reorder_begin_it{std::next(reordering_vec.begin(), day_start_idx)};
    auto reorder_end_it{std::next(reordering_vec.begin(), day_end_idx)};
    std::sort(reorder_begin_it, reorder_end_it,
              [&](const size_t i, const size_t j) { return reorder_cmp(i + case_offset, j + case_offset); });
}

// Process a single day by iterating over the input until we either find the start of the next day or the case is ended.
// Returns the index of the first element after the last element within the day.
size_t handle_next_day(size_t day_start_idx, const size_t case_start_idx, size_t case_end_idx,
                       const DeconstructedInput& input, DeconstructedOutputColumn& output_data) {
    auto get_cur_day{[&](const size_t row) -> std::optional<TimestampValue> {
        return extract_day(input.timestamp_data.elements->get(row).get_timestamp());
    }};

    bool day_based_activity_found{false};

    // Once we have found the boundaries of a day we can call this to reorder the day.
    auto reorder_day_lmb{[&](const size_t day_start_idx, const size_t day_end_idx) {
        // only need to reorder if we actually encountered a day based activity or have more than one activity in the day.
        if (day_based_activity_found && day_end_idx - day_start_idx >= 2) {
            reorder_day(day_start_idx, day_end_idx, case_start_idx, input, output_data);
        }
    }};

    // Skip all null timestamps at the start of the case.
    if (input.timestamp_data.null_elements != nullptr) {
        while (day_start_idx < case_end_idx && input.timestamp_data.null_elements->at(day_start_idx)) {
            day_start_idx++;
        }
    }

    // Get the current day and iterate over the array until either the end of the case is reached or the day changes.
    auto day{get_cur_day(day_start_idx)};
    for (size_t row{day_start_idx}; row < case_end_idx; row++) {
        if (day != get_cur_day(row)) {
            reorder_day_lmb(day_start_idx, row);
            return row;
        }

        day_based_activity_found |= input.is_day_based_data.elements->get(row).get_uint8();
    }

    // We still need to potentially reorder the final day in the case.
    reorder_day_lmb(day_start_idx, case_end_idx);

    return case_end_idx;
}

// Handle a single case by iterating over all days we find in that case.
void handle_case(DeconstructedOutputColumn& output_data, const DeconstructedInput& input, const size_t case_start_idx,
                 const size_t case_end_idx) {
    size_t current_row{case_start_idx};

    while (current_row < case_end_idx) {
        current_row = handle_next_day(current_row, case_start_idx, case_end_idx, input, output_data);
    }
}

/* Enum for distinguishing inputs other than the timestamps. */
enum class input_type { IS_DAY_BASED, SORTING };

/* Getter for the name of a given input parameter. */
std::string get_name_for_input(const input_type input_type) {
    if (input_type == input_type::IS_DAY_BASED) {
        return "is_day_based";
    }
    return "sorting";
}

/* Getter for the data of a given input parameter. */
const UnnestedArrayData get_data_for_input_type(const DeconstructedInput& input, const input_type input_type) {
    if (input_type == input_type::IS_DAY_BASED) {
        return input.is_day_based_data;
    }
    return input.sorting_data;
}

/* Checks whether the size of the timestamp data is equal to the size of the data of a given other input. */
Status check_data_size(const DeconstructedInput& input, const input_type input_type) {
    const auto timestamp_data_size{input.timestamp_data.elements->size()};
    const auto other_data_size{get_data_for_input_type(input, input_type).elements->size()};
    if (timestamp_data_size != other_data_size) {
        return Status::InvalidArgument(
                fmt::format("Mismatch between size of timestamp elements {} and size of {} elements {}.",
                            input.timestamp_data.elements->size(), get_name_for_input(input_type),
                            input.is_day_based_data.elements->size()));
    }
    return Status::OK();
}

/* Checks whether the null flag for a specific case of a given input are equal to the expected flag. */
Status check_null_flag(const DeconstructedInput& input, const input_type input_type, const size_t case_idx,
                       bool expected_flag) {
    const auto* other_null_arrays{get_data_for_input_type(input, input_type).null_arrays};

    const std::string other_name{get_name_for_input(input_type)};

    if (expected_flag) {
        // Verify that other is null
        if (other_null_arrays == nullptr || !other_null_arrays->at(case_idx)) {
            return Status::InvalidArgument(
                    fmt::format("Timestamp array in row {} is null, but corresponding array of {} is not null.",
                                case_idx, other_name));
        }
    } else {
        // Verify that other is not null
        if (other_null_arrays != nullptr && other_null_arrays->at(case_idx)) {
            return Status::InvalidArgument(
                    fmt::format("Timestamp array in row {} is not null, but corresponding array of {} is null.",
                                case_idx, other_name));
        }
    }
    return Status::OK();
}

/* Checks whether the boundaries of a specific case for the timestamp are input are equal to those of another input. */
Status check_case_boundaries(const DeconstructedInput& input, const input_type input_type, const size_t case_idx) {
    auto timestamp_case_start_idx{input.timestamp_data.offsets->get_data()[case_idx]};
    auto timestamp_case_end_idx{input.timestamp_data.offsets->get_data()[case_idx + 1]};
    auto other_case_start_idx{get_data_for_input_type(input, input_type).offsets->get_data()[case_idx]};
    auto other_case_end_idx{get_data_for_input_type(input, input_type).offsets->get_data()[case_idx + 1]};
    if (timestamp_case_start_idx != other_case_start_idx || timestamp_case_end_idx != other_case_end_idx) {
        return Status::InvalidArgument(
                fmt::format("Array {} of timestamp input has case boundaries [{}, {}], but the corresponding array of "
                            "{} has boundaries [{}, {}].",
                            case_idx, timestamp_case_start_idx, timestamp_case_end_idx, get_name_for_input(input_type),
                            other_case_start_idx, other_case_end_idx));
    }

    return Status::OK();
}

Status adjust_daily_timestamps_impl(DeconstructedOutputColumn& output_data, const DeconstructedInput& input) {
    const auto& case_offsets{input.timestamp_data.offsets->get_data()};

    // Sanity checks confirming that input arrays have same size
    RETURN_IF_ERROR(check_data_size(input, input_type::IS_DAY_BASED));
    RETURN_IF_ERROR(check_data_size(input, input_type::SORTING));

    // Main loop computing the output data. Basically iterate over all cases and handle each case separately.
    for (size_t case_idx{0}; case_idx < input.num_cases; case_idx++) {
        // If the case is null we can skip it
        if (input.timestamp_data.null_arrays != nullptr && input.timestamp_data.null_arrays->at(case_idx)) {
            // Sanity checks confirming that other arrays are null as well
            RETURN_IF_ERROR(check_null_flag(input, input_type::IS_DAY_BASED, case_idx, true));
            RETURN_IF_ERROR(check_null_flag(input, input_type::SORTING, case_idx, true));
            continue;
        }

        // Sanity checks confirming that other arrays are also not null
        RETURN_IF_ERROR(check_null_flag(input, input_type::IS_DAY_BASED, case_idx, false));
        RETURN_IF_ERROR(check_null_flag(input, input_type::SORTING, case_idx, false));

        auto case_start_idx{case_offsets[case_idx]};
        auto case_end_idx{case_offsets[case_idx + 1]};

        // Sanity checks for array boundaries
        RETURN_IF_ERROR(check_case_boundaries(input, input_type::IS_DAY_BASED, case_idx));
        RETURN_IF_ERROR(check_case_boundaries(input, input_type::SORTING, case_idx));

        handle_case(output_data, input, case_start_idx, case_end_idx);
    }

    // The above loop computes the projections from sorted to unsorted timestamps. The reordering that we want to return
    // is the projection from unsorted to sorted timestamps. So we invert the reordering here (need to create a copy as
    // we cannot do that in-place).
    auto sorted_to_unsorted_cpy{output_data.output_reordering_elements->get_data()};
    for (size_t case_idx{0}; case_idx < input.num_cases; case_idx++) {
        auto case_start_idx{case_offsets[case_idx]};
        auto case_end_idx{case_offsets[case_idx + 1]};

        // We iterate over the case and invert. We need to be careful as indices in the elements array are absolute but
        // values in that array are relative to the case start.
        for (size_t i{case_start_idx}; i < case_end_idx; i++) {
            auto sorted_idx{i};
            auto unsorted_idx{sorted_to_unsorted_cpy[i] + case_start_idx};
            output_data.output_reordering_elements->get_data()[unsorted_idx] = sorted_idx - case_start_idx;
        }
    }

    return Status::OK();
}

} // namespace

StatusOr<ColumnPtr> CelonisAdjustDailyTimestamps::celonis_adjust_daily_timestamps(FunctionContext* context,
                                                                                  const Columns& columns) {
    DCHECK(columns.size() == 2 || columns.size() == 3);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    auto* input_timestamps{columns[0].get()};

    auto output_data{create_output_column(*input_timestamps)};

    // If no sorting column is defined we can return early
    if (columns.size() == 2) {
        return std::move(output_data.final_output);
    }

    auto* input_day_based_flags{columns[1].get()};
    auto* input_sorting{columns[2].get()};

    DeconstructedInput input{.timestamp_data = prepare_array_input(input_timestamps),
                             .is_day_based_data = prepare_array_input(input_day_based_flags),
                             .sorting_data = prepare_array_input(input_sorting),
                             .num_cases = columns[0]->size()};

    auto status{adjust_daily_timestamps_impl(output_data, input)};

    if (status.ok()) {
        return std::move(output_data.final_output);
    }
    return status;
}

} // namespace starrocks