#pragma once

#include <array>
#include <limits>

#include <boost/date_time/gregorian/greg_calendar.hpp>

#include <ctl/assert.h>
#include <ctl/type_traits.h>

#include "modules/common/int_types.h"

namespace celonis::accelerator {
namespace date {

/**
 * @brief used as a placeholder for an invalid year as the value of
 * INVALID_YEAR is outside of the range of valid years (i.e., 9999).
 */
static constexpr uint16_t INVALID_YEAR = std::numeric_limits<uint16_t>::max();
/**
 * @brief This constant can be used to check whether the return value of a call to
 * celonis_date_storage::get_calendar_week() is a valid calendar week.
 * For days in the range [1500-1-1, 2500-2-1), celonis_date_storage::get_calendar_week()
 * returns the correct calendar week and outside of this range INVALID_CALENDAR_WEEK
 * is returned.
 */
static constexpr uint8_t INVALID_CALENDAR_WEEK = std::numeric_limits<uint8_t>::max();
/**
 * @brief used as a placeholder for an invalid day number (since epoch) as
 * the value of INVALID_DAY_NUMBER is outside of the range of valid day numbers.
 */
static constexpr uint32_t INVALID_DAY_NUMBER = std::numeric_limits<uint32_t>::max();

/**
 * @brief small and simple month class. Not an enum class because it provides some utility functions.
 */
class month_type final {
 public:
  enum month : uint8_t {
    JANUARY = 0,
    FEBRUARY = 1,
    MARCH = 2,
    APRIL = 3,
    MAY = 4,
    JUNE = 5,
    JULY = 6,
    AUGUST = 7,
    SEPTEMBER = 8,
    OCTOBER = 9,
    NOVEMBER = 10,
    DECEMBER = 11
  };

  [[nodiscard]] static constexpr uint8_t NUMBER_OF_MONTHS() noexcept { return 12; }

  /*constructors*/
  constexpr explicit month_type(month v) noexcept : month_value(v) {}

  /*getter*/
  [[nodiscard]] constexpr auto enum_val() const noexcept { return month_value; }
  [[nodiscard]] constexpr auto type_val() const noexcept { return ctl::enum_to_underlying_type(month_value); }
  constexpr explicit operator uint8_t() const noexcept { return type_val(); }
  constexpr operator size_t() const noexcept { return type_val(); }  // NOLINT(google-explicit-constructor)

  /*comparison operators*/
  [[nodiscard]] constexpr bool operator==(month_type rhs) const noexcept { return enum_val() == rhs.enum_val(); }
  [[nodiscard]] constexpr bool operator==(month v) const noexcept { return enum_val() == v; }
  [[nodiscard]] constexpr bool operator!=(month_type rhs) const noexcept { return !operator==(rhs); }
  [[nodiscard]] constexpr bool operator!=(month v) const noexcept { return !operator==(v); }
  [[nodiscard]] constexpr bool operator<(month_type rhs) const noexcept { return type_val() < rhs.type_val(); }
  [[nodiscard]] constexpr bool operator<(month v) const noexcept {
    return type_val() < ctl::enum_to_underlying_type(v);
  }
  [[nodiscard]] constexpr bool operator<=(month_type rhs) const noexcept { return operator<(rhs) || operator==(rhs); }
  [[nodiscard]] constexpr bool operator<=(month v) const noexcept { return operator<(v) || operator==(v); }
  [[nodiscard]] constexpr bool operator>(month_type rhs) const noexcept { return type_val() > rhs.type_val(); }
  [[nodiscard]] constexpr bool operator>(month v) const noexcept {
    return type_val() > ctl::enum_to_underlying_type(v);
  }
  [[nodiscard]] constexpr bool operator>=(month_type rhs) const noexcept { return operator>(rhs) || operator==(rhs); }
  [[nodiscard]] constexpr bool operator>=(month v) const noexcept { return operator>(v) || operator==(v); }

  /*misc*/
  month_type& operator++() noexcept {
    // do not replace by a lookup table or static_cast<month>((type_val() + 1) % 12)
    month_value = month_value == DECEMBER ? JANUARY : static_cast<month>(type_val() + 1);
    return *this;
  }

  [[nodiscard]] constexpr auto days() const noexcept {
    /** does not consider leap years */
    constexpr std::array<uint8_t, NUMBER_OF_MONTHS()> EXPECTED_DAYS_IN_MONTH = {31, 28, 31, 30, 31, 30,
                                                                                31, 31, 30, 31, 30, 31};
    return EXPECTED_DAYS_IN_MONTH.at(type_val());
  }

 private:
  month month_value;
};

[[nodiscard]] inline const char* to_string(const month_type month) noexcept {
  switch (month.enum_val()) {
    case month_type::JANUARY:
      return "JANUARY";
    case month_type::FEBRUARY:
      return "FEBRUARY";
    case month_type::MARCH:
      return "MARCH";
    case month_type::APRIL:
      return "APRIL";
    case month_type::MAY:
      return "MAY";
    case month_type::JUNE:
      return "JUNE";
    case month_type::JULY:
      return "JULY";
    case month_type::AUGUST:
      return "AUGUST";
    case month_type::SEPTEMBER:
      return "SEPTEMBER";
    case month_type::OCTOBER:
      return "OCTOBER";
    case month_type::NOVEMBER:
      return "NOVEMBER";
    case month_type::DECEMBER:
      return "DECEMBER";
  }
  return "INVALID";
}

static constexpr month_type JAN{month_type::JANUARY};
static constexpr month_type FEB{month_type::FEBRUARY};
static constexpr month_type MAR{month_type::MARCH};
static constexpr month_type APR{month_type::APRIL};
static constexpr month_type MAY{month_type::MAY};
static constexpr month_type JUN{month_type::JUNE};
static constexpr month_type JUL{month_type::JULY};
static constexpr month_type AUG{month_type::AUGUST};
static constexpr month_type SEP{month_type::SEPTEMBER};
static constexpr month_type OCT{month_type::OCTOBER};
static constexpr month_type NOV{month_type::NOVEMBER};
static constexpr month_type DEC{month_type::DECEMBER};

/**
 * @brief some date related constants (preferable to magic numbers)
 */
static constexpr size_t NUMBER_OF_WEEKDAYS = 7;
static constexpr size_t NUMBER_OF_MONTHS = month_type::NUMBER_OF_MONTHS();
static constexpr size_t MAX_DAYS_IN_MONTH = 31;
static constexpr size_t MAX_DAYS_IN_YEAR = 366;
static constexpr size_t MONTHS_PER_QUARTER = 3;

/* time related constants */
static constexpr int32_t MILLIS_PER_SECOND = 1000;
static constexpr int32_t SECONDS_PER_MINUTE = 60;
static constexpr int32_t MINUTES_PER_HOUR = 60;
static constexpr int32_t HOURS_PER_DAY = 24;

static constexpr int32_t MILLIS_PER_MINUTE = MILLIS_PER_SECOND * SECONDS_PER_MINUTE;
static constexpr int32_t MILLIS_PER_HOUR = MILLIS_PER_MINUTE * MINUTES_PER_HOUR;
static constexpr int32_t MILLIS_PER_DAY = MILLIS_PER_HOUR * HOURS_PER_DAY;
static constexpr int32_t MILLIS_PER_WEEK = MILLIS_PER_DAY * NUMBER_OF_WEEKDAYS;

static constexpr double DAYS_PER_GREG_YEAR{365.2425};
static constexpr double DAYS_PER_GREG_MONTH{30.436875};

/* date related constants */

/**
 * Min/Max possible year (dictated by boost).
 */
static constexpr uint16_t MIN_POSSIBLE_YEAR = 1400;
static constexpr uint16_t MAX_POSSIBLE_YEAR = 9999;

[[nodiscard]] static constexpr bool is_valid_year(const uint16_t year) noexcept {
  return MIN_POSSIBLE_YEAR <= year && year <= MAX_POSSIBLE_YEAR;
}

/**
 * @param year Ensure year to be between 1400 and 9999 (both inclusive).
 * @param month Ensure month to be between 1 and 12.
 * @param day Ensure day is valid for the given month and year. It may differ for the leap years.
 * @return true if a date is valid, false otherwise
 */
[[nodiscard]] static constexpr bool is_valid_date(const uint16_t year, const uint16_t month,
                                                  const uint16_t day) noexcept {
  if (!is_valid_year(year)) {
    return false;
  }
  if (month < 1 || month > NUMBER_OF_MONTHS) {
    return false;
  }
  if (day < 1 || boost::gregorian::gregorian_calendar::end_of_month_day(year, month) < day) {
    return false;
  }
  return true;
}

}  // namespace date

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_seconds(const unsigned long long seconds) noexcept -> int64_t {
  debug_assert(seconds <=
               static_cast<unsigned long long>(std::numeric_limits<int64_t>::max() / date::MILLIS_PER_SECOND));
  return static_cast<int64_t>(seconds) * date::MILLIS_PER_SECOND;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_minutes(const unsigned long long minutes) noexcept -> int64_t {
  debug_assert(minutes <=
               static_cast<unsigned long long>(std::numeric_limits<int64_t>::max() / date::MILLIS_PER_MINUTE));
  return static_cast<int64_t>(minutes) * date::MILLIS_PER_MINUTE;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_hours(const unsigned long long hours) noexcept -> int64_t {
  debug_assert(hours <= static_cast<unsigned long long>(std::numeric_limits<int64_t>::max() / date::MILLIS_PER_HOUR));
  return static_cast<int64_t>(hours) * date::MILLIS_PER_HOUR;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_days(const unsigned long long days) noexcept -> int64_t {
  debug_assert(days <= static_cast<unsigned long long>(std::numeric_limits<int64_t>::max() / date::MILLIS_PER_DAY));
  return static_cast<int64_t>(days) * date::MILLIS_PER_DAY;
}

// NOLINTNEXTLINE(google-runtime-int)
constexpr auto operator""_weeks(const unsigned long long weeks) noexcept -> int64_t {
  debug_assert(weeks <= static_cast<unsigned long long>(std::numeric_limits<int64_t>::max() / date::MILLIS_PER_WEEK));
  return static_cast<int64_t>(weeks) * date::MILLIS_PER_WEEK;
}

}  // namespace celonis::accelerator
