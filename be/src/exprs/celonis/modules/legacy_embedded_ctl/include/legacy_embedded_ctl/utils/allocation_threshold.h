#pragma once

#include <memory>

#include "legacy_embedded_ctl/utils/allocation_priority.h"

namespace celonis::accelerator::legacy_embedded_ctl::utils {

/**
 * @brief Represents a threshold percentage of memory. Currently this is used to represent the threshold of memory which
 * needs to be available after a memory allocation.
 */
class allocation_threshold final {
 public:
  explicit allocation_threshold(double value);
  allocation_threshold& operator=(double value);
  [[nodiscard]] double get() const noexcept;
  [[nodiscard]] double as_percentage() const noexcept;
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator double() const noexcept;

 private:
  double value_;
};

[[nodiscard]] allocation_threshold allocation_priority_to_allocation_threshold(utils::allocation_priority priority);

/**
 * @brief Singleton holding the allocation threshold configurations as configured during launch of the engine
 */
class allocation_threshold_config final {
 public:
  allocation_threshold_config(const allocation_threshold_config&) = delete;
  allocation_threshold_config& operator=(const allocation_threshold_config&) = delete;
  allocation_threshold_config(allocation_threshold_config&&) = delete;
  allocation_threshold_config& operator=(allocation_threshold_config&&) = delete;
  ~allocation_threshold_config() noexcept = default;

  /** Initializes the singleton */
  static void initialize(double default_memory_allocation_threshold);

  [[nodiscard]] static const allocation_threshold_config& instance();

  [[nodiscard]] allocation_threshold default_threshold() const noexcept;
  [[nodiscard]] allocation_threshold low_priority_threshold() const noexcept;
  [[nodiscard]] allocation_threshold medium_priority_threshold() const;
  [[nodiscard]] allocation_threshold high_priority_threshold() const;

 private:
  explicit allocation_threshold_config(allocation_threshold default_threshold) noexcept;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  inline static std::unique_ptr<allocation_threshold_config> initialized_instance_{nullptr};
  allocation_threshold default_memory_threshold_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl::utils
