#pragma once

#include <string>
#include <unordered_map>

namespace celonis::accelerator::common::tracing::sampling {
/**
 * Maps a span name to a sampling rate, a real number in [0, 1].
 * 0.0 = discard all spans with the specified name, 1.0 = keep all spans with the specified name.
 */
using span_sampling_rules_t = std::unordered_map<std::string, double>;
}  // namespace celonis::accelerator::common::tracing::sampling