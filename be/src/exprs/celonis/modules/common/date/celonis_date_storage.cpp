#include "celonis_date_storage.h"

#include <boost/date_time/posix_time/posix_time.hpp>

namespace celonis::accelerator::date {

std::string celonis_date_storage::to_iso_8601_timestamp() const {
  // Strip out millis
  boost::posix_time::ptime pt(date, boost::posix_time::seconds(get_day_millis() / MILLIS_PER_SECOND));
  return to_iso_extended_string(pt);
}

}  // namespace celonis::accelerator::date
