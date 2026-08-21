#pragma once

#include <ctime>
#include <ostream>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/functional/hash.hpp>

#include <ctl/assert.h>

#include "modules/common/date/date_time_constants.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::date {

static constexpr uint16_t EPOCH_YEAR = 1970;
const uint32_t REF_DATE = boost::gregorian::date(EPOCH_YEAR, 1, 1).day_number();  // NOLINT(cert-err58-cpp)

/**
 * Please note, that several of the provided date modification member functions of 'celonis_date_storage' do no input
 * sanitization or date validity checks after modification for performance reasons. Directly using these date
 * modification member functions is generally not intended if you are not producing their inputs yourself (and can thus
 * guarantee the validity of the request). For date creation/modification with validity checks please use the provided
 * utility factories in celonis_date_storae_utils.h. By default (or a template argument 'CHECKED=true') these utility
 * functions do validity checks. If you do not need validity checks, it is recommended to still use the utility
 * functions with 'false' as a template argument (e.g., 'from_timestamp_millis<false>(..)').
 */
class celonis_date_storage {
 public:
  using date_t = boost::gregorian::date;
  using time_t = int32_t;
  /* constructors */
  /**
   * @brief constructs a celonis_date_storage object with the epoch time: 1970-01-01 00:00:00
   */
  celonis_date_storage();

  /**
   * @brief constructs a celonis_date_storage object with the internal boost::gregorian::date object initialized
   * according to the passed parameters and the time since midnight set to zero
   * @param year the year to initliaze the internal boost::gregorian::date object with
   * @param month the month to initliaze the internal boost::gregorian::date object with
   * @param day the day to initliaze the internal boost::gregorian::date object with
   */
  celonis_date_storage(uint16_t year, uint16_t month, uint16_t day);

  /**
   * @brief constructs a celonis_date_storage with the given date and milliseconds (time of day).
   * @param date The date to use.
   * @param milliseconds The milliseconds to add (by means of add_millis) to the result.
   */
  celonis_date_storage(date_t date, time_t milliseconds);

  /**
   * @brief constructs a celonis_date_storage with the given std::tm object.
   */
  explicit celonis_date_storage(const std::tm& time);

  /**
   * @brief constructs a celonis_date_storage object with the internal boost::gregorian::date object initialized
   * according to the passed parameters year, month, day and the time since midnight set to according to the
   * parameters hours, minute, seconds and milliseconds
   * @param year the year to initliaze the internal boost::gregorian::date object with
   * @param month the month to initliaze the internal boost::gregorian::date object with
   * @param day the day to initliaze the internal boost::gregorian::date object with
   * @param hour the hour since midnight within the current day
   * @param minute the minute since midnight within the current day
   * @param second the second since midnight within the current day
   * @param millisecond the millisecond since midnight within the current day
   */
  celonis_date_storage(uint16_t year, uint16_t month, uint16_t day, int hour, int minute, int second,
                       int32_t millisecond);

  /**
   * @brief More readable version of the default constructor.
   */
  [[nodiscard]] static celonis_date_storage EPOCH();

  /* comparison operators */

  // TODO(lwolf): investigate why conventional comparison/equality operators are not auto-deduced from operator<=>
  std::strong_ordering operator<=>(const celonis_date_storage& other) const noexcept;
  bool operator==(const celonis_date_storage& rhs) const noexcept;
  bool operator!=(const celonis_date_storage& rhs) const noexcept;
  bool operator<(const celonis_date_storage& rhs) const noexcept;
  bool operator<=(const celonis_date_storage& rhs) const noexcept;
  bool operator>(const celonis_date_storage& rhs) const noexcept;
  bool operator>=(const celonis_date_storage& rhs) const noexcept;

  /* getters for the respective time units of the celonis_date_storage */
  /**
   * @return the number of passed milliseconds of that day (since midnight)
   */
  [[nodiscard]] time_t get_day_millis() const noexcept;
  /**
   * @return the internal boost::gregorian::date object
   */
  [[nodiscard]] date_t get_date() const noexcept;
  [[nodiscard]] uint16_t get_year() const;
  [[nodiscard]] uint16_t get_quarter() const;
  [[nodiscard]] uint16_t get_month() const;
  /**
   * @brief this is wrapper call to boost::gregorian::date::day_number().
   * @note the boost documentation says, it returns the absolute numbers of days since the epoch start
   * (@see https://www.boost.org/doc/libs/1_53_0/doc/html/date_time/gregorian.html#gregcal_functions).
   * However, this is not true or inconsistent with boost's definition of their epoch start:
   * boost::gregorian::gregorian_calendar::epoch() returns 1400-1-1 but calling day_number() on this epoch
   * returns a non-zero number (i.e., not the number of days since epoch). There is also a StackOverflow article
   * (@see https://stackoverflow.com/questions/46055536/boostgregoriandate-day-number-returns-julian-date)
   * regarding this. Therefore, this function should be avoided if possible.
   * @return [not entirely sure]
   */
  [[nodiscard]] uint32_t get_day_number() const;
  [[nodiscard]] uint16_t get_day_of_year() const;
  /**
   * @return the boost representation of a weekday (range: Sun == 0 to Sat == 6)
   */
  [[nodiscard]] boost::gregorian::greg_weekday get_day_of_week() const;
  /**
   * @return the 'day' part of the date (e.g., returns 15 for 1970-07-15)
   */
  [[nodiscard]] uint16_t get_day() const;
  [[nodiscard]] uint32_t get_julian_day() const;
  [[nodiscard]] int32_t get_hours() const noexcept;
  [[nodiscard]] int32_t get_minutes() const noexcept;
  [[nodiscard]] int32_t get_seconds() const noexcept;
  [[nodiscard]] int32_t get_millis() const noexcept;

  /* setters for both the internal boost::gregorian date object and the time since midnight */
  void set_date(uint16_t year, uint16_t month, uint16_t day);
  void set_time(int32_t hour, int32_t minute, int32_t second, int32_t millisecond) noexcept;
  void set_day_millis(time_t day_millis) noexcept;

  /* functions for rounding a celonis_date_storage to the respective time unit */
  [[nodiscard]] celonis_date_storage round_year() const;
  [[nodiscard]] celonis_date_storage round_quarter() const;
  [[nodiscard]] celonis_date_storage round_month() const;
  [[nodiscard]] celonis_date_storage round_week() const;
  [[nodiscard]] celonis_date_storage round_day() const;
  [[nodiscard]] celonis_date_storage round_hour() const;
  [[nodiscard]] celonis_date_storage round_minute() const;
  [[nodiscard]] celonis_date_storage round_second() const;

  /*
   * Functions to add the respective time units to the celonis_date_storage. Boost might throw an exception inheriting
   * from std::out_of_range, however this seems to be not guaranteed. Must check is_invalid_date afterwards.
   */
  void add_years(int16_t years);
  void add_months(int32_t months);
  void add_days(int32_t days);
  void add_hours(int32_t hours);
  void add_minutes(int64_t minutes);
  void add_seconds(int64_t seconds);
  void add_millis(int64_t millis);

  /* difference between two dates */
  [[nodiscard]] double millis_between(const celonis_date_storage& other) const;
  [[nodiscard]] double seconds_between(const celonis_date_storage& other) const;
  [[nodiscard]] double minutes_between(const celonis_date_storage& other) const;
  [[nodiscard]] double hours_between(const celonis_date_storage& other) const;
  [[nodiscard]] double days_between(const celonis_date_storage& other) const;
  [[nodiscard]] double months_between(const celonis_date_storage& other) const;
  [[nodiscard]] double years_between(const celonis_date_storage& other) const;

  /* difference between two dates without fractions */
  [[nodiscard]] int64_t full_minutes_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_hours_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_days_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_weeks_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_months_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_quarters_between(const celonis_date_storage& other) const;
  [[nodiscard]] int64_t full_years_between(const celonis_date_storage& other) const;

  /* misc */
  [[nodiscard]] bool is_invalid_date() const noexcept;
  [[nodiscard]] int64_t to_timestamp() const;
  void from_timestamp(int64_t ts);
  [[nodiscard]] std::string to_iso_8601_timestamp() const;

  /**
   * @return the ctime representation of a date object
   */
  [[nodiscard]] std::tm to_tm() const;

 private:
  int32_t day_milliseconds{0};
  boost::gregorian::date date{EPOCH_YEAR, 1, 1};
};

[[nodiscard]] inline celonis_date_storage::date_t min_possible_date() {
  static const boost::gregorian::date min_date{MIN_POSSIBLE_YEAR, 1, 1};
  return min_date;
}

[[nodiscard]] inline celonis_date_storage::date_t max_possible_date() {
  static const boost::gregorian::date max_date{MAX_POSSIBLE_YEAR, 12, 31};
  return max_date;
}

[[nodiscard]] inline celonis_date_storage min_possible_timestamp() {
  static const celonis_date_storage min_timestamp{MIN_POSSIBLE_YEAR, 1, 1, 0, 0, 0, 0};
  return min_timestamp;
}

[[nodiscard]] inline celonis_date_storage max_possible_timestamp() {
  static const celonis_date_storage max_timestamp{MAX_POSSIBLE_YEAR, 12, 31, 23, 59, 59, 999};
  return max_timestamp;
}

inline std::ostream& operator<<(std::ostream& os, celonis_date_storage const& m) { return os << m.to_timestamp(); }

inline celonis_date_storage::celonis_date_storage() = default;

inline celonis_date_storage::celonis_date_storage(uint16_t year, uint16_t month, uint16_t day)
    : date(year, month, day) {}

inline celonis_date_storage::celonis_date_storage(const boost::gregorian::date date, const int32_t milliseconds)
    : date(date) {
  add_millis(milliseconds);
}

inline celonis_date_storage::celonis_date_storage(const tm& time)
    : day_milliseconds{(MILLIS_PER_SECOND *
                        (time.tm_sec + SECONDS_PER_MINUTE * (time.tm_min + MINUTES_PER_HOUR * time.tm_hour)))},
      date{boost::gregorian::date_from_tm(time)} {
  debug_assert(0 <= day_milliseconds && day_milliseconds < MILLIS_PER_DAY);
}

inline celonis_date_storage::celonis_date_storage(uint16_t year, uint16_t month, uint16_t day, int hour, int minute,
                                                  int second, int millisecond)
    : date(year, month, day) {
  const int millis =
      millisecond + (MILLIS_PER_SECOND * (second + SECONDS_PER_MINUTE * (minute + MINUTES_PER_HOUR * hour)));
  if (0 <= millis && millis < MILLIS_PER_DAY) {
    day_milliseconds = millis;
  } else {
    add_millis(millis);
  }
}

inline celonis_date_storage celonis_date_storage::EPOCH() { return celonis_date_storage(); }

inline std::strong_ordering celonis_date_storage::operator<=>(const celonis_date_storage& other) const noexcept {
  if (date < other.date) {
    return std::strong_ordering::less;
  }

  if (date > other.date) {
    return std::strong_ordering::greater;
  }

  return day_milliseconds <=> other.day_milliseconds;
}

inline bool celonis_date_storage::operator==(const celonis_date_storage& rhs) const noexcept {
  return date == rhs.date && day_milliseconds == rhs.day_milliseconds;
}

inline bool celonis_date_storage::operator!=(const celonis_date_storage& rhs) const noexcept {
  return day_milliseconds != rhs.day_milliseconds || date != rhs.date;
}

inline bool celonis_date_storage::operator<(const celonis_date_storage& rhs) const noexcept {
  return date < rhs.date || (date == rhs.date && day_milliseconds < rhs.day_milliseconds);
}

inline bool celonis_date_storage::operator<=(const celonis_date_storage& rhs) const noexcept {
  return date < rhs.date || (date == rhs.date && day_milliseconds <= rhs.day_milliseconds);
}

inline bool celonis_date_storage::operator>(const celonis_date_storage& rhs) const noexcept {
  return date > rhs.date || (date == rhs.date && day_milliseconds > rhs.day_milliseconds);
}

inline bool celonis_date_storage::operator>=(const celonis_date_storage& rhs) const noexcept {
  return date > rhs.date || (date == rhs.date && day_milliseconds >= rhs.day_milliseconds);
}

inline int32_t celonis_date_storage::get_day_millis() const noexcept { return day_milliseconds; }

inline boost::gregorian::date celonis_date_storage::get_date() const noexcept { return date; }

inline uint16_t celonis_date_storage::get_year() const { return date.year(); }

inline uint16_t celonis_date_storage::get_quarter() const {
  return static_cast<uint16_t>(((get_month() - 1u) / 3u) + 1u);
}

inline uint16_t celonis_date_storage::get_month() const { return date.month(); }

inline uint32_t celonis_date_storage::get_day_number() const {
  // Absolute number of days since the epoch start
  return date.day_number();
}

inline uint16_t celonis_date_storage::get_day_of_year() const { return date.day_of_year(); }

inline boost::gregorian::greg_weekday celonis_date_storage::get_day_of_week() const { return date.day_of_week(); }

inline uint16_t celonis_date_storage::get_day() const { return date.day(); }

inline uint32_t celonis_date_storage::get_julian_day() const { return date.julian_day(); }

inline int32_t celonis_date_storage::get_hours() const noexcept {
  return (day_milliseconds / date::MILLIS_PER_HOUR) % HOURS_PER_DAY;
}

inline int32_t celonis_date_storage::get_minutes() const noexcept {
  return (day_milliseconds / date::MILLIS_PER_MINUTE) % MINUTES_PER_HOUR;
}

inline int32_t celonis_date_storage::get_seconds() const noexcept {
  return (day_milliseconds / date::MILLIS_PER_SECOND) % SECONDS_PER_MINUTE;
}

inline int32_t celonis_date_storage::get_millis() const noexcept { return day_milliseconds % MILLIS_PER_SECOND; }

inline void celonis_date_storage::set_date(uint16_t year, uint16_t month, uint16_t day) {
  date = boost::gregorian::date(year, month, day);
}

inline void celonis_date_storage::set_time(int32_t hour, int32_t minute, int32_t second, int32_t millisecond) noexcept {
  set_day_millis(millisecond +
                 (MILLIS_PER_SECOND * (second + SECONDS_PER_MINUTE * (minute + MINUTES_PER_HOUR * hour))));
}

inline void celonis_date_storage::set_day_millis(int32_t day_millis) noexcept { day_milliseconds = day_millis; }

inline celonis_date_storage celonis_date_storage::round_year() const {
  return celonis_date_storage(get_year(), 1u, 1u);
}

inline celonis_date_storage celonis_date_storage::round_quarter() const {
  const uint16_t month = static_cast<uint16_t>((((get_month() - 1u) / 3u) * 3u) + 1u);
  return celonis_date_storage(get_year(), month, 1u);
}

inline celonis_date_storage celonis_date_storage::round_month() const {
  return celonis_date_storage(get_year(), get_month(), 1u);
}

inline celonis_date_storage celonis_date_storage::round_week() const {
  using boost::gregorian::days;
  // US style, sunday = 0, monday = 1, ...
  // but cpm4 uses european style, week starts on monday
  days days_to_begin_of_week;
  switch (get_day_of_week().as_number()) {
    case 0:
      days_to_begin_of_week = days{-6};
      break;
    case 1:
      days_to_begin_of_week = days{0};
      break;
    case 2:
      days_to_begin_of_week = days{-1};
      break;
    case 3:
      days_to_begin_of_week = days{-2};
      break;
    case 4:
      days_to_begin_of_week = days{-3};
      break;
    case 5:
      days_to_begin_of_week = days{-4};
      break;
    case 6:
      days_to_begin_of_week = days{-5};
      break;
  }
  celonis_date_storage target;
  target.date = date + days_to_begin_of_week;
  return target;
}

inline celonis_date_storage celonis_date_storage::round_day() const {
  return celonis_date_storage(get_year(), get_month(), get_day());
}

inline celonis_date_storage celonis_date_storage::round_hour() const {
  return celonis_date_storage(get_year(), get_month(), get_day(), get_hours(), 0, 0, 0);
}

inline celonis_date_storage celonis_date_storage::round_minute() const {
  return celonis_date_storage(get_year(), get_month(), get_day(), get_hours(), get_minutes(), 0, 0);
}

inline celonis_date_storage celonis_date_storage::round_second() const {
  return celonis_date_storage(get_year(), get_month(), get_day(), get_hours(), get_minutes(), get_seconds(), 0);
}

inline void celonis_date_storage::add_years(int16_t years) { date += boost::gregorian::years(years); }
inline void celonis_date_storage::add_months(int32_t months) { date += boost::gregorian::months(months); }
inline void celonis_date_storage::add_days(int32_t days) { date += boost::gregorian::days(days); }
inline void celonis_date_storage::add_hours(int32_t hours) {
  add_millis(static_cast<int64_t>(hours) * MILLIS_PER_HOUR);
}
inline void celonis_date_storage::add_minutes(int64_t minutes) { add_millis(minutes * MILLIS_PER_MINUTE); }
inline void celonis_date_storage::add_seconds(int64_t seconds) { add_millis(seconds * MILLIS_PER_SECOND); }

/**
 * As for the other add_[unit] functions, we do not check and sanitize the input to be in a valid range.
 * e.g., adding ((10000 - 1400) * 365 * 24 * 60 * 60 * 1000)ms to a date after the year 1400 would overflow
 * the supported date range of 1400.1.1 - 9999.1.1
 * We only guarantee that adding a valid number of milliseconds (e.g., 2.5 billion) does not overflow the
 * internal milliseconds for the day
 */
inline void celonis_date_storage::add_millis(int64_t millis) {
  millis += day_milliseconds;
  // First, calculate the number of days the given milliseconds represent
  const int64_t number_of_days = millis / date::MILLIS_PER_DAY;
  // Second, calculate the remaining number of milliseconds to add
  const int64_t remaining_millis = millis % date::MILLIS_PER_DAY;
  // Third, add days and continue with millisecond arithmetic
  add_days(static_cast<int32_t>(number_of_days));
  day_milliseconds = static_cast<int32_t>(remaining_millis);
  // Handle case where day_milliseconds is now negative: need to go a day back
  if (day_milliseconds < 0) {
    add_days(-1);
    day_milliseconds += date::MILLIS_PER_DAY;
  }
}

inline double celonis_date_storage::millis_between(const celonis_date_storage& other) const {
  return static_cast<double>(other.to_timestamp() - this->to_timestamp());
}
inline double celonis_date_storage::seconds_between(const celonis_date_storage& other) const {
  return static_cast<double>(other.to_timestamp() - this->to_timestamp()) / MILLIS_PER_SECOND;
}
inline double celonis_date_storage::minutes_between(const celonis_date_storage& other) const {
  return static_cast<double>(other.to_timestamp() - this->to_timestamp()) / MILLIS_PER_MINUTE;
}
inline double celonis_date_storage::hours_between(const celonis_date_storage& other) const {
  return static_cast<double>(other.to_timestamp() - this->to_timestamp()) / MILLIS_PER_HOUR;
}
inline double celonis_date_storage::days_between(const celonis_date_storage& other) const {
  return static_cast<double>(this->full_days_between(other)) +
         static_cast<double>(other.day_milliseconds - this->day_milliseconds) / MILLIS_PER_DAY;
}
inline double celonis_date_storage::months_between(const celonis_date_storage& other) const {
  return static_cast<double>(this->full_days_between(other)) / DAYS_PER_GREG_MONTH;
}
inline double celonis_date_storage::years_between(const celonis_date_storage& other) const {
  return static_cast<double>(this->full_days_between(other)) / DAYS_PER_GREG_YEAR;
}

inline int64_t celonis_date_storage::full_minutes_between(const celonis_date_storage& other) const {
  const auto rounded_this{this->round_minute()};
  const auto rounded_other{other.round_minute()};
  return static_cast<int64_t>(rounded_other.to_timestamp() - rounded_this.to_timestamp()) /
         static_cast<int64_t>(MILLIS_PER_MINUTE);
}
inline int64_t celonis_date_storage::full_hours_between(const celonis_date_storage& other) const {
  const auto rounded_this{this->round_hour()};
  const auto rounded_other{other.round_hour()};
  return static_cast<int64_t>(rounded_other.to_timestamp() - rounded_this.to_timestamp()) /
         static_cast<int64_t>(MILLIS_PER_HOUR);
}
inline int64_t celonis_date_storage::full_days_between(const celonis_date_storage& other) const {
  // Need to cast the day number to int64_t, because internally it is an *unsigned* 32 bit integer which would not
  // correctly fit into an int32_t.
  return static_cast<int64_t>(other.get_day_number()) - static_cast<int64_t>(this->get_day_number());
}
inline int64_t celonis_date_storage::full_weeks_between(const celonis_date_storage& other) const {
  const auto rounded_this{this->round_week()};
  const auto rounded_other{other.round_week()};
  return static_cast<int64_t>(rounded_this.full_days_between(rounded_other)) / static_cast<int64_t>(NUMBER_OF_WEEKDAYS);
}
inline int64_t celonis_date_storage::full_months_between(const celonis_date_storage& other) const {
  return static_cast<int64_t>(this->full_years_between(other)) * static_cast<int64_t>(NUMBER_OF_MONTHS) +
         static_cast<int64_t>(other.get_month()) - static_cast<int64_t>(this->get_month());
}
inline int64_t celonis_date_storage::full_quarters_between(const celonis_date_storage& other) const {
  const auto rounded_this{this->round_quarter()};
  const auto rounded_other{other.round_quarter()};
  return static_cast<int64_t>(rounded_this.full_months_between(rounded_other)) /
         static_cast<int64_t>(MONTHS_PER_QUARTER);
}
inline int64_t celonis_date_storage::full_years_between(const celonis_date_storage& other) const {
  return static_cast<int64_t>(other.get_year()) - static_cast<int64_t>(this->get_year());
}

inline bool celonis_date_storage::is_invalid_date() const noexcept {
  static constexpr uint32_t MIN_DAY_NUMBER = 2232400;  // boost::gregorian::date{1400, 1, 1}.day_number();
  debug_assert(MIN_DAY_NUMBER == (boost::gregorian::date{date::MIN_POSSIBLE_YEAR, 1, 1}.day_number()));
  static constexpr uint32_t MAX_DAY_NUMBER = 5373484;  // boost::gregorian::date{9999, 12, 31}.day_number();
  debug_assert(MAX_DAY_NUMBER == (boost::gregorian::date{date::MAX_POSSIBLE_YEAR, 12, 31}.day_number()));
  return date.is_infinity() || date.is_not_a_date() || date.day_number() < MIN_DAY_NUMBER ||
         date.day_number() > MAX_DAY_NUMBER;
}

inline int64_t celonis_date_storage::to_timestamp() const {
  // the timestamp is relative to 1970-01-01
  return ((static_cast<int64_t>(date.day_number()) - date::REF_DATE) * date::MILLIS_PER_DAY) + day_milliseconds;
}

inline void celonis_date_storage::from_timestamp(int64_t ts) {
  set_date(EPOCH_YEAR, 1, 1);
  set_day_millis(0);
  // By adding to a boost::gregorian::date, the valid date range [1400,9999] can be exceeded without triggering an
  // exception This is because boost::gregorian::date supports (+/-) infinities. The caller is responsible to check
  // for validity, e.g., by calling is_invalid_date()
  add_millis(ts);
}
inline std::tm celonis_date_storage::to_tm() const {
  auto tm{boost::gregorian::to_tm(this->date)};
  tm.tm_sec = this->get_seconds();
  tm.tm_min = this->get_minutes();
  tm.tm_hour = this->get_hours();
  return tm;
}

[[nodiscard]] inline std::size_t hash_value(const celonis_date_storage& date) {
  size_t result = 0;
  boost::hash_combine(result, date.to_timestamp());
  return result;
}

}  // namespace celonis::accelerator::date

namespace std {

template <>
struct hash<celonis::accelerator::date::celonis_date_storage> {
  size_t operator()(const celonis::accelerator::date::celonis_date_storage& date) const noexcept {
    return celonis::accelerator::date::hash_value(date);
  }
};

inline celonis::accelerator::date::celonis_date_storage min(
    const celonis::accelerator::date::celonis_date_storage& lhs,
    const celonis::accelerator::date::celonis_date_storage& rhs) noexcept {
  return rhs < lhs ? rhs : lhs;
}

inline celonis::accelerator::date::celonis_date_storage max(
    const celonis::accelerator::date::celonis_date_storage& lhs,
    const celonis::accelerator::date::celonis_date_storage& rhs) noexcept {
  return lhs < rhs ? rhs : lhs;
}

}  // namespace std
