#include "legacy_embedded_ctl/hyperloglog.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace celonis::accelerator::legacy_embedded_ctl {

hyperloglog::hyperloglog() { std::ranges::fill(registers_, 0); }

hyperloglog& hyperloglog::operator+=(const hyperloglog& other) {
  for (size_t idx{0}; idx < other.registers_.size(); ++idx) {
    registers_[idx] = std::max(registers_[idx], other.registers_[idx]);
  }

  return *this;
}

size_t hyperloglog::cardinality() const {
  constexpr size_t NUM_REGISTERS_SQUARED{1ull << (LOG_NUM_REGISTERS << 1)};

  // Correction factor for systemic bias in the harmonic mean estimate.
  constexpr auto ALPHA{0.7213 / (1.0 + 1.079 / static_cast<double>(NUM_REGISTERS))};

  // Normalizes the harmonic mean of register values to a valid cardinality estimate.
  constexpr auto NORMALIZATION_FACTOR{ALPHA * NUM_REGISTERS_SQUARED};

  auto unnormalized_harmonic_mean{0.0};
  for (const auto estimate : registers_) {
    unnormalized_harmonic_mean += 1.0 / static_cast<double>(1ull << estimate);
  }

  auto estimate{correct_bias(NORMALIZATION_FACTOR / unnormalized_harmonic_mean)};
  return static_cast<size_t>(estimate);
}

double hyperloglog::correct_bias(double estimate) const {
  // Apply "small-range correction" from Flajolet et al., 2007.
  if (estimate <= 2.5 * NUM_REGISTERS) {
    auto zeros{std::ranges::count(registers_, 0)};
    if (zeros != 0) {
      estimate = NUM_REGISTERS * std::log(NUM_REGISTERS / static_cast<double>(zeros));
    }
  }

  // NOTE: Flajolet et al., 2007 additionally specifies a "large-value" correction to correct
  // for bias induced by hash collisions when the number of observed samples is large. However,
  // the paper was based on a 32-bit hash functions for which hash collisions become likley
  // when observing >10^9 elements. Since this implementation uses a 64-bit hash function
  // (where hash collisions are vastly less likely), we do not correct for large values.

  return estimate;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
