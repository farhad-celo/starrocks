#include "eventlog_utils.h"

namespace starrocks::celonis {

ActivitiesBuilder& ActivitiesBuilder::add_day_based_activity(std::string name, const int64_t sorting_val) {
    metadata_.activity_day_based.emplace(name, true);
    metadata_.activity_sorting.emplace(std::move(name), sorting_val);
    return *this;
}

ActivitiesBuilder& ActivitiesBuilder::add_non_day_based_activity(std::string name, const int64_t sorting_val) {
    metadata_.activity_day_based.emplace(name, false);
    metadata_.activity_sorting.emplace(std::move(name), sorting_val);
    return *this;
}

ActivityMetadata ActivitiesBuilder::build() {
    return std::move(metadata_);
}

CaseBuilder::CaseBuilder(const starrocks::celonis::ActivityMetadata& metadata) : metadata_{metadata} {}

CaseBuilder& CaseBuilder::add_row(const std::string& activity, const starrocks::TimestampValue timestamp) {
    data_.timestamps.emplace_back(std::move(timestamp));
    data_.is_day_based.emplace_back(metadata_.activity_day_based.at(activity));
    data_.sorting.emplace_back(metadata_.activity_sorting.at(activity));
    return *this;
}

CaseBuilder& CaseBuilder::add_row_with_missing_metadata(const std::string& activity,
                                                        const starrocks::TimestampValue timestamp) {
    data_.timestamps.emplace_back(std::move(timestamp));
    data_.is_day_based.emplace_back(kNullDatum);
    data_.sorting.emplace_back(kNullDatum);
    return *this;
}

CaseBuilder& CaseBuilder::add_null_timestamp(const std::string& activity) {
    data_.timestamps.emplace_back(kNullDatum);
    data_.is_day_based.emplace_back(metadata_.activity_day_based.at(activity));
    data_.sorting.emplace_back(metadata_.activity_sorting.at(activity));
    return *this;
}

CaseData CaseBuilder::build() {
    return std::move(data_);
}

EventlogScenarioBuilder& EventlogScenarioBuilder::add_case(starrocks::celonis::CaseData data) {
    timestamps_.emplace_back(std::move(data.timestamps));
    is_day_based_.emplace_back(std::move(data.is_day_based));
    sorting_.emplace_back(std::move(data.sorting));
    return *this;
}

EventlogScenarioBuilder& EventlogScenarioBuilder::add_null_case() {
    timestamps_.emplace_back(kNullDatum);
    is_day_based_.emplace_back(kNullDatum);
    sorting_.emplace_back(kNullDatum);
    return *this;
}

EventlogScenario EventlogScenarioBuilder::build() {
    return EventlogScenario{.timestamp_column = set_up_eventlog_column<TimestampValue>(timestamps_),
                            .sorting_column = set_up_eventlog_column<int64_t>(sorting_),
                            .is_day_based_column = set_up_eventlog_column<uint8_t>(is_day_based_)};
}

TimestampValue create_day_based_timestamp(int day) {
    return TimestampValue::create(0, 0, day, 0, 0, 0, 0);
}

TimestampValue create_timestamp(int day, int hour) {
    return TimestampValue::create(0, 0, day, hour, 0, 0, 0);
}

} // namespace starrocks::celonis