#include <random>
#include <vector>

#include "modules/query/calendars.pb.h"

namespace starrocks {

using UniformInt = std::uniform_int_distribution<int32_t>;
using CreateCalendar = std::function<celonis::accelerator::Calendar(const std::vector<std::string>, std::mt19937&)>;

inline celonis::accelerator::Calendar create_factory_calendar(const std::vector<std::string>& calendar_ids,
                                                              std::mt19937& rng, int num_calendar_entries) {
    UniformInt uniform_timestamp_increase(1, 3600 * 24 * 365 * 10); // 10 years

    celonis::accelerator::FactoryCalendar factory_calendar;
    for (auto i = 0; i < calendar_ids.size(); ++i) {
        const auto& calendar_id = calendar_ids[i];
        for (auto j = 0; j < num_calendar_entries; ++j) {
            celonis::accelerator::FactoryCalendarEntry* entry = factory_calendar.add_entries();
            int unix_seconds = uniform_timestamp_increase(rng);
            entry->set_start_date(unix_seconds * 1000);
            entry->set_end_date((unix_seconds + 3600) * 1000); // the size of the entry is 1 hour.
            entry->set_calendar_id(calendar_id);
        }
    }

    celonis::accelerator::Calendar calendar_proto;
    *calendar_proto.mutable_factory_calendar() = factory_calendar;
    return calendar_proto;
}
inline celonis::accelerator::Calendar create_weekday_calendar(const std::vector<std::string>& calendar_ids,
                                                              std::mt19937& rng) {
    celonis::accelerator::MultiWeekdayCalendar multi_weekday_calendar;
    for (auto i = 0; i < calendar_ids.size(); ++i) {
        celonis::accelerator::WeekdayCalendar* weekday_calendar = multi_weekday_calendar.add_calendars();
        const std::string& calendar_id = calendar_ids[i];
        weekday_calendar->set_calendar_id(calendar_id);

        for (celonis::accelerator::WeekdayCalendarEntry* entry : {
                     weekday_calendar->mutable_monday(),
                     weekday_calendar->mutable_tuesday(),
                     weekday_calendar->mutable_wednesday(),
                     weekday_calendar->mutable_thursday(),
                     weekday_calendar->mutable_friday(),
             }) {
            entry->set_use_day(true);
            entry->mutable_shift()->set_begin(0);
            entry->mutable_shift()->set_end(86400000);
        }
        for (celonis::accelerator::WeekdayCalendarEntry* entry : {
                     weekday_calendar->mutable_saturday(),
                     weekday_calendar->mutable_sunday(),
             }) {
            entry->set_use_day(false);
        }
    }

    celonis::accelerator::Calendar calendar_proto;
    *calendar_proto.mutable_multi_weekday_calendar() = multi_weekday_calendar;
    return calendar_proto;
}

} // namespace starrocks