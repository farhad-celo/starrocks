#pragma once

#include <limits>
#include <memory>
#include <string>

#include "modules/common/int_types.h"

namespace celonis::accelerator::memory::management {

constexpr size_t LARGE_SWAP_FILE_THRESHOLD{std::numeric_limits<int32_t>::max()};

/**
 * @brief describes the persistence status of a swapped object:
 * - PERSISTENT: is swappable and is persistently stored on disk (e.g., for data model data)
 * - NON_PERSISTENT: is swappable but can also be safely erased from disk (e.g., cached- or other temporary data)
 */
enum class persistence_status : int8_t { PERSISTENT, NON_PERSISTENT };

/**
 * @brief collection of various swap-related meta-data required for swapping data to disk.
 */
class swap_info {
 public:
  /** getter */
  [[nodiscard]] bool is_swappable() const noexcept;
  [[nodiscard]] persistence_status persistence_state() const noexcept;
  [[nodiscard]] bool is_persistent() const noexcept;
  [[nodiscard]] bool is_non_persistent() const noexcept;
  [[nodiscard]] const std::string& base_directory() const noexcept;
  [[nodiscard]] const std::string& encryption_key() const noexcept;
  [[nodiscard]] std::string& encryption_key() noexcept;
  [[nodiscard]] bool is_no_swap() const noexcept;

  [[nodiscard]] std::string get_swap_file_path(const std::string& swap_file) const;

  [[nodiscard]] std::string get_tmp_swap_file_path(const std::string& swap_file) const;

  [[nodiscard]] swap_info swap_into_sub_dir(const std::string& sub_dir, persistence_status persistence_state) const;
  [[nodiscard]] swap_info swap_into_sub_dir(const std::string& sub_dir) const;

  [[nodiscard]] swap_info copy_and_set_persistence(persistence_status persistence_state) const;

 private:
  friend swap_info no_swap();
  swap_info() = default;  // TODO(n.weber): noexcept with C++20 and its noexcept default string constructors

  // TODO(n.weber): CPL-4374 check if this flag can be replaced by extending the persistence_status
  bool swappable{false};
  std::string swap_base_directory{};
  persistence_status swap_persistence_state{persistence_status::NON_PERSISTENT};
  std::string swap_encryption_key{};
};

inline bool swap_info::is_swappable() const noexcept { return swappable; }

inline persistence_status swap_info::persistence_state() const noexcept { return swap_persistence_state; }

inline bool swap_info::is_persistent() const noexcept { return persistence_state() == persistence_status::PERSISTENT; }

inline bool swap_info::is_non_persistent() const noexcept {
  return persistence_state() == persistence_status::NON_PERSISTENT;
}

inline const std::string& swap_info::base_directory() const noexcept { return swap_base_directory; }

inline const std::string& swap_info::encryption_key() const noexcept { return swap_encryption_key; }

inline std::string& swap_info::encryption_key() noexcept { return swap_encryption_key; }

inline bool swap_info::is_no_swap() const noexcept { return true; }

[[nodiscard]] swap_info no_swap();

}  // namespace celonis::accelerator::memory::management
