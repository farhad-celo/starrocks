#include "tracing_options.h"

#include <iostream>
#include <string>

#include <boost/algorithm/string/predicate.hpp>

namespace celonis::accelerator::common::tracing {

std::istream& operator>>(std::istream& in, mode& trace_mode) {
  std::string mode_as_string;
  in >> mode_as_string;
  if (boost::iequals(mode_as_string, "OFF")) {
    trace_mode = mode::OFF;
    return in;
  }
  if (boost::iequals(mode_as_string, "REMOTE")) {
    trace_mode = mode::REMOTE;
    return in;
  }

  trace_mode = mode::OFF;  // default mode
  return in;
}

std::ostream& operator<<(std::ostream& os, const mode& trace_mode) {
  switch (trace_mode) {
    case mode::OFF:
      return os << "OFF";
    case mode::REMOTE:
      return os << "REMOTE";
  }
  // for compiler correctness
  return os << "UNKNOWN";
}

}  // namespace celonis::accelerator::common::tracing
