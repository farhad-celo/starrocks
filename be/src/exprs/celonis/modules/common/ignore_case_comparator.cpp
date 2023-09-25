#include "ignore_case_comparator.h"

#include <algorithm>

#include <boost/algorithm/string.hpp>

namespace celonis::accelerator::common {

bool ignore_case_comparator_less::operator()(const std::string& lhs, const std::string& rhs) const {
  return std::lexicographical_compare(std::cbegin(lhs), std::cend(lhs), std::cbegin(rhs), std::cend(rhs),
                                      boost::algorithm::is_iless());
}

bool ignore_case_comparator_equal::operator()(const std::string& lhs, const std::string& rhs) const {
  return std::ranges::equal(lhs, rhs, boost::algorithm::is_iequal{});
}

size_t ignore_case_hasher::operator()(std::string key) const {
  boost::to_lower(key);
  return std::hash<std::string>{}(key);
}

}  // namespace celonis::accelerator::common
