#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/byte_literals.h"
#include "legacy_embedded_ctl/memory/meminfo_fwd.h"
#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Converts the two given meminfos to a single JSON object.
 *
 * @details
 * The function reports the usage (globally used memory divided by totally available memory) of the old and new meminfo.
 * Additionally a direct representation of the old status (total available memory, globally reserved memory, process
 * reserved memory) is reported, together with the changes in the new status.
 *
 * If either usage is missing in the report, something went wrong when creating the corresponding meminfo.
 *
 * @note This function does not escape any special JSON characters in the given strings, as it
 * is only intended to generate logging output!
 */
[[nodiscard]] legacy_embedded_format::json::json_object_t meminfo_change_json(const full_meminfo& old_meminfo,
                                                                              const full_meminfo& new_meminfo,
                                                                              std::optional<size_t> not_swapped_bytes,
                                                                              const std::string& status);

/**
 * @brief Factory function which returns a meminfo instance with the current memory sizes set
 */
meminfo fetch_current_meminfo();

full_meminfo fetch_current_full_meminfo();

/**
 * @brief Log memory state of the net allocated memory consumption state is not 0. To be called before the DM process
 * exits.
 */
void log_if_net_allocated_nonzero();

/**
 * @brief Represents system information for main memory
 */
class meminfo {
 public:
  using size_type = std::size_t;

  constexpr meminfo(size_type page_size, size_type total, size_type available, size_type in_use_by_process) noexcept;

  /**
   * @brief Returns the page size of the system (in the given unit; default KiB)
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type page_size() const noexcept;

  /**
   * @brief Returns the total memory size of the system (in the given unit; default KiB)
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type total() const noexcept;

  /**
   * @brief Returns the available memory size of the system (in the given unit; default KiB)
   *
   * The available memory size includes memory which is currently used (e.g., for caching) but which is generally
   * available for processes to allocate
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type available() const noexcept;

  /**
   * @brief Returns the used memory size of the system (in the given unit; default KiB)
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type in_use_global() const noexcept;

  /**
   * @brief Returns the used memory size of the current process (in the given unit; default KiB)
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type in_use_by_process() const noexcept;

  /**
   * @brief Returns the percentage of the used memory size of the system
   */
  [[nodiscard]] constexpr std::optional<double> in_use_global_percentage() const noexcept;

 private:
  size_type page_size_in_bytes_{0};
  size_type total_in_bytes_{0};
  size_type available_in_bytes_{0};
  size_type in_use_by_process_in_bytes_{0};
};

/**
 * @brief Represents system information for main memory and the process's peak memory consumption
 */
class full_meminfo : private meminfo {
 public:
  using size_type = std::size_t;

  constexpr full_meminfo(size_type page_size, size_type total, size_type available, size_type in_use_by_process,
                         size_type peak_consumption_by_process, size_type current_estimate_in_bytes) noexcept;

  using meminfo::available;
  using meminfo::in_use_by_process;
  using meminfo::in_use_global;
  using meminfo::in_use_global_percentage;
  using meminfo::page_size;
  using meminfo::total;

  /**
   * @brief Returns the peak memory consumption of the current process
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type peak_consumption_by_process() const noexcept;

  /**
   * @brief Returns the current estimate from the CTL memory tracker
   */
  template <byte_unit TARGET_BYTE_UNIT = byte_unit::KiB>
  [[nodiscard]] constexpr size_type current_estimate() const noexcept;

 private:
  size_type peak_consumption_by_process_in_bytes_{0};
  size_type current_estimate_in_bytes_{0};
};

constexpr meminfo::meminfo(const size_type page_size, const size_type total, const size_type available,
                           const size_type in_use_by_process) noexcept
    : page_size_in_bytes_{page_size},
      total_in_bytes_{total},
      available_in_bytes_{available},
      in_use_by_process_in_bytes_{in_use_by_process} {}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type meminfo::page_size() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(page_size_in_bytes_);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type meminfo::total() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(total_in_bytes_);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type meminfo::available() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(available_in_bytes_);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type meminfo::in_use_global() const noexcept {
  legacy_embedded_debug_assert(total_in_bytes_ >= available_in_bytes_);
  const size_t memory_in_use = total_in_bytes_ - available_in_bytes_;
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(memory_in_use);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type meminfo::in_use_by_process() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(in_use_by_process_in_bytes_);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type full_meminfo::peak_consumption_by_process() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(peak_consumption_by_process_in_bytes_);
}

template <byte_unit TARGET_BYTE_UNIT>
constexpr meminfo::size_type full_meminfo::current_estimate() const noexcept {
  return convert_byte_size<byte_unit::B, TARGET_BYTE_UNIT>(current_estimate_in_bytes_);
}

constexpr std::optional<double> meminfo::in_use_global_percentage() const noexcept {
  constexpr auto almost_equals = [](const double x, const double y, const int precision_gap) noexcept -> bool {
    return std::abs(x - y) <= std::numeric_limits<double>::epsilon() * std::abs(x + y) * precision_gap ||
           std::abs(x - y) < std::numeric_limits<double>::min();
  };

  const double total_d = static_cast<double>(total_in_bytes_);
  // TODO(n.weber): first compare with 0 unnecessary
  if (total_d == 0 || almost_equals(total_d, 0, 100)) {
    return std::nullopt;
  }
  const double mem_in_use = static_cast<double>(in_use_global<byte_unit::B>());
  return mem_in_use / total_d;
}

constexpr full_meminfo::full_meminfo(const size_type page_size, const size_type total, const size_type available,
                                     const size_type in_use_by_process, const size_type peak_consumption_by_process,
                                     const size_type current_estimate_in_bytes) noexcept
    : meminfo{page_size, total, available, in_use_by_process},
      peak_consumption_by_process_in_bytes_{peak_consumption_by_process},
      current_estimate_in_bytes_{current_estimate_in_bytes} {}

}  // namespace celonis::accelerator::legacy_embedded_ctl
