#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Implements the HyperLogLog algorithm from [Flajolet et al.,
 * 2007](https://algo.inria.fr/flajolet/Publications/FlFuGaMe07.pdf).
 *
 * @details HyperLogLog estimates the cardinality (i.e., number of unique elements) of a multiset by observing
 *  the number of leading zero-bits of hash values of observed elements. The time complexity of HyperLogLog is
 *  O(N) where N is the number of observed elements in the multiset. The space complexity of HyperLogLog is
 *  small and constant. This implementation uses 256 registers and thus only uses 256 bytes / 4 cache lines per
 *  thread. Given  uniformly-distributed 64-bit hash values, the relative error of the estimate produced by
 *  this implementation is bounded by \f$\frac{1.04}{\sqrt{2^8}} \approx 0.01625\f$.
 */
class hyperloglog {
 public:
  hyperloglog();

  /**
   * @brief Inserts an element from the observed multiset.
   *
   * @param hash is the 64-bit hash value of the inserted element.
   */
  void insert(std::uint64_t hash) {
    constexpr size_t HASH_WIDTH{64};

    // The first log_num_estimates bits are used to identify the register. The remaining bits
    // (the "suffix") are used to compute the estimate itself.
    static constexpr size_t SUFFIX_WIDTH{HASH_WIDTH - LOG_NUM_REGISTERS};

    const size_t register_index{hash >> SUFFIX_WIDTH};
    const size_t suffix{hash << LOG_NUM_REGISTERS};

    auto estimate{static_cast<uint8_t>(1 + __builtin_clzl(suffix | (1ULL << (LOG_NUM_REGISTERS - 1))))};
    registers_[register_index] = std::max(registers_[register_index], estimate);
  }

  /**
   * @brief Integrates observations from the argument hyperloglog instance.
   *
   * @details Combining observations from several hyperloglog instances effectively creates a cardinality
   *  estimator for the joint data observed by all combined instances. This can be used for parallelizing
   *  hyperloglog by partitioning the multiset and analyzing each partition with an independent hyperloglog
   *  instance and then combine the results. Note that in such a scenario, hash values must be generated
   *  from the same hash function across all partitions to produce a meaningful result. The combination
   *  (i.e., this method) has constant runtime complexity.
   */
  hyperloglog& operator+=(const hyperloglog& other);

  /**
   * @brief Returns the estimated cardinality of the observed multiset.
   */
  [[nodiscard]] size_t cardinality() const;

 private:
  // Setting the number of registers statically (rather than, as a constructor parameter as done in
  // off-the-shelf HyperLogLog implementations) allows pre-computing certain operands at compile-time.
  // The specific value (2^8) has been determined by microbenchmarking, as a trade-off between a low
  // error bound and runtime performance.
  static constexpr size_t LOG_NUM_REGISTERS{8};
  static constexpr size_t NUM_REGISTERS{1ULL << LOG_NUM_REGISTERS};

  std::array<uint8_t, NUM_REGISTERS> registers_{};

  /**
   * @brief Corrects bias in the estimate due to an insufficient number of observed samples.
   */
  [[nodiscard]] double correct_bias(double estimate) const;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl