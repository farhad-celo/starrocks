#pragma once

#include <optional>
#include <vector>

#include "column/array_column.h"
#include "column/column.h"
#include "column/nullable_column.h"

namespace starrocks::celonis {

/* Represents metadata about activities used within a event log. */
struct ActivityMetadata {
    std::unordered_map<std::string, bool> activity_day_based;
    std::unordered_map<std::string, int64_t> activity_sorting;
};

/* Can be used to construct activity metadata. */
class ActivitiesBuilder {
public:
    /* Adds a day-based activity with a sorting value to the event log. */
    ActivitiesBuilder& add_day_based_activity(std::string name, int64_t sorting_val);

    /* Adds a non-day-based activity with a sorting value to the event log. */
    ActivitiesBuilder& add_non_day_based_activity(std::string name, int64_t sorting_val);

    /* Construct the activity metadata, will make this object unusable. */
    ActivityMetadata build();

private:
    ActivityMetadata metadata_;
};

/* Represents the data content of a single case in an event log. */
struct CaseData {
    DatumArray timestamps{};
    DatumArray is_day_based{};
    DatumArray sorting{};
};

/* Can be used to fill a single event log case with data and build it. */
class CaseBuilder {
public:
    /* Must be initialized with activity metadata, only the activities defined there shall be used. */
    CaseBuilder(const ActivityMetadata& metadata);

    /* Adds a row for a given activity and timestamp to the case. */
    CaseBuilder& add_row(const std::string& activity, TimestampValue timestamp);

    /* Adds a row for a given activity with a null timestamp to the case. */
    CaseBuilder& add_null_timestamp(const std::string& activity);

    /* Adds a row for a given activity and timestamp to the case, where all other metadata fields are null. */
    CaseBuilder& add_row_with_missing_metadata(const std::string& activity, TimestampValue timestamp);

    /* Constructs a case representation from the builder, will make this object unusable. */
    CaseData build();

private:
    const ActivityMetadata& metadata_;
    CaseData data_{};
};

/* Represents an entire event log used for testing. */
struct EventlogScenario {
    ColumnPtr timestamp_column;
    ColumnPtr sorting_column;
    ColumnPtr is_day_based_column;
};

/* Can be used to fill an event log scenario with cases. */
class EventlogScenarioBuilder {
public:
    /* Adds a single case to the event log. */
    EventlogScenarioBuilder& add_case(CaseData data);

    /* Adds a null case to the event log, i.e. a an array for which the null flag is set. */
    EventlogScenarioBuilder& add_null_case();

    /* Constructs an event log scenario from the builder, will make this object unusable. */
    EventlogScenario build();

private:
    DatumArray timestamps_{};
    DatumArray is_day_based_{};
    DatumArray sorting_{};
};

/* Creates a day-based timestamp for a given day. */
TimestampValue create_day_based_timestamp(int day);

/* Creates a timestamp for a given day and hour. */
TimestampValue create_timestamp(int day, int hour);

/* Set up a nullable event log column for data of a given type. The input is a DatumArray representing the cases. Each
 * datum in the datum array is again expected to be a DatumArray. */
template <typename T>
ColumnPtr set_up_eventlog_column(DatumArray data) {
    auto inner_null_flags = NullColumn::create();
    auto elements = FixedLengthColumn<T>::create();
    auto offsets = UInt32Column::create();
    auto outer_null_flags = NullColumn::create();
    auto nullable_elements = NullableColumn::create(std::move(elements), std::move(inner_null_flags));
    auto array_column = ArrayColumn::create(std::move(nullable_elements), std::move(offsets));
    auto nullable_arrays = NullableColumn::create(std::move(array_column), std::move(outer_null_flags));

    std::for_each(data.begin(), data.end(),
                  [&nullable_arrays](const Datum& datum) { nullable_arrays->append_datum(datum); });

    nullable_arrays->check_or_die();
    return nullable_arrays;
}

} // namespace starrocks::celonis