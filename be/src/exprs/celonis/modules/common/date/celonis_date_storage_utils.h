#pragma once

#include <type_traits>

#include <boost/date_time/gregorian/gregorian.hpp>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/do_check.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::date {

/**
 *
 * @param days, number of days from the Unix epoch, 1 January 1970
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_date(int64_t days) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  celonis_date_storage result;
  result.add_days(static_cast<int32_t>(days));
  if constexpr (DO_CHECK_TAG::value) {
    if (result.is_invalid_date()) {
      throw boost::gregorian::bad_year{};
    }
  }
  return result;
}

/**
 *
 * @param micros, number of microseconds from the Unix epoch, 00:00:00.000000 on 1 January 1970, UTC
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_timestamp_micros(int64_t micros) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  celonis_date_storage result;
  result.from_timestamp(static_cast<int64_t>(micros / 1000));
  if constexpr (DO_CHECK_TAG::value) {
    if (result.is_invalid_date()) {
      throw boost::gregorian::bad_year{};
    }
  }
  return result;
}

/**
 *
 * @param millis, number of milliseconds from the Unix epoch, 00:00:00.000000 on 1 January 1970, UTC
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_timestamp_millis(int64_t millis) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  celonis_date_storage result;
  result.from_timestamp(millis);
  if constexpr (DO_CHECK_TAG::value) {
    if (result.is_invalid_date()) {
      throw boost::gregorian::bad_year{};
    }
  }
  return result;
}

/**
 *
 * @param days, number of days from the Unix epoch, 1 January 1970
 * @param millis
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_days_and_millis(int64_t days, int64_t millis) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  celonis_date_storage result;
  result.add_days(static_cast<int32_t>(days));
  result.add_millis(millis);
  if constexpr (DO_CHECK_TAG::value) {
    if (result.is_invalid_date()) {
      throw boost::gregorian::bad_year{};
    }
  }
  return result;
}

/**
 *
 * @param julian_day_number, number of days from noon of universal time, 24 November 4714 BC (Gregorian calendar)
 * @param millis
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_julian_day_number_and_millis(int64_t julian_day_number, int64_t millis) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  static constexpr uint32_t JULIAN_REF_DATE = 2440588;  // boost::gregorian::date(1970, 1, 1).julian_day()
  legacy_embedded_debug_assert(JULIAN_REF_DATE == boost::gregorian::date(1970, 1, 1).julian_day());
  const int64_t days = julian_day_number - JULIAN_REF_DATE;
  // If specified by 'DO_CHECK_TAG', assumes the validity check is done in 'from_days_and_millis'
  return from_days_and_millis<DO_CHECK_TAG>(days, millis);
}

/**
 * @brief creates a date object for a given year and day within this year.
 * @param year, valid range [1400..9999]
 * @param day_of_year zero based day of the year
 * @return
 */
template <typename DO_CHECK_TAG = common::do_check<true>>
inline celonis_date_storage from_year_day(const uint16_t year, const uint16_t day_of_year) {
  static_assert(
      std::is_same_v<DO_CHECK_TAG, common::do_check<true>> || std::is_same_v<DO_CHECK_TAG, common::do_check<false>>,
      "Template parameter 'TAG' must be of type 'do_check<bool>'.");
  celonis_date_storage result;
  result.add_years(static_cast<int16_t>(year - EPOCH_YEAR));
  result.add_days(day_of_year);
  if constexpr (DO_CHECK_TAG::value) {
    if (result.is_invalid_date()) {
      throw boost::gregorian::bad_year{};
    }
  }
  return result;
}

/**
 * Converts a milliseconds timestamp into days
 * @param timestamp, duration in milliseconds
 * @return days
 */
inline int64_t day_timestamp_converter(int64_t timestamp) { return timestamp / MILLIS_PER_DAY; }

/**
 * Converts a date object into days
 * @param value
 * @return days
 */
inline int64_t day_date_converter(const celonis_date_storage& value) {
  return day_timestamp_converter(value.to_timestamp());
}

/**
 * Converts a milliseconds timestamp into hours
 * @param timestamp, duration in milliseconds
 * @return hours
 */
inline int64_t hour_timestamp_converter(int64_t timestamp) { return timestamp / MILLIS_PER_HOUR; }

/**
 * Converts date objects into hours
 * @param value
 * @return hours
 */
inline int64_t hour_date_converter(const celonis_date_storage& value) {
  return hour_timestamp_converter(value.to_timestamp());
}

/**
 * Converts a milliseconds timestamp into minutes
 * @param timestamp, duration in milliseconds
 * @return minutes
 */
inline int64_t minute_timestamp_converter(int64_t timestamp) { return timestamp / MILLIS_PER_MINUTE; }

/**
 * Converts date objects into minutes
 * @param value
 * @return minutes
 */
inline int64_t minute_date_converter(const celonis_date_storage& value) {
  return minute_timestamp_converter(value.to_timestamp());
}

/**
 * Converts a milliseconds timestamp into seconds
 * @param timestamp, duration in milliseconds
 * @return seconds
 */
inline int64_t second_timestamp_converter(int64_t timestamp) { return timestamp / MILLIS_PER_SECOND; }

/**
 * Converts date objects into seconds
 * @param value
 * @return seconds
 */
inline int64_t second_date_converter(const celonis_date_storage& value) {
  return second_timestamp_converter(value.to_timestamp());
}

/**
 * Converts a milliseconds timestamp into milliseconds
 * @param timestamp, duration in milliseconds
 * @return milliseconds
 */
inline int64_t millisecond_timestamp_converter(int64_t timestamp) { return timestamp; }

/**
 * Converts date objects into milliseconds
 * @param value
 * @return milliseconds
 */
inline int64_t millisecond_date_converter(const celonis_date_storage& value) {
  return millisecond_timestamp_converter(value.to_timestamp());
}

inline bool is_leap_year(const uint16_t year) { return boost::gregorian::gregorian_calendar::is_leap_year(year); }

/**
 * Get the number of days for a year
 * @param year
 * @return days
 * @pre year must be in the valid range [1400, 9999] (otherwise throws boost::gregorian::bad_year exception)
 */
inline uint16_t number_of_days(const uint16_t year) { return is_leap_year(year) ? 366u : 365u; }

/**
 * Get the number of days for a month
 * @param year the year (required to determine leap years)
 * @param month the month for which the number of days shall be returned
 * @return number of days in month
 * @pre year must be in the valid range [1400, 9999] (otherwise throws boost::gregorian::bad_year exception)
 */
inline uint8_t number_of_days(const uint16_t year, const month_type month) {
  if (is_leap_year(year) && month.enum_val() == month_type::FEBRUARY) {
    return 29;
  }
  return month.days();
}

}  // namespace celonis::accelerator::date
