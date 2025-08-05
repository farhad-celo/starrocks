#pragma once
#include <bytell_hash_map.hpp>

#include "aligned_blocked_range.h"
#include "legacy_embedded_ctl/array_view.h"
#include "modules/common/buffer_types.h"
#include "shared_types_fwd.h"

namespace celonis::accelerator::common {

/**
 * Wrapper around char_buffer that makes use of a best effort dictionary of the previously allocated strings. This aims
 * to reduce the overhead of allocating strings.
 */
class output_string_buffer {
 public:
  using size_type = size_t;

  // Maximum number of elements that we store in the map. When we have more unique elements we do not add them to the
  // map anymore
  static constexpr size_t MAP_CAPACITY{1024};
  enum class strategy_t { MATERIALIZED, BEST_EFFORT_DICTIONARY };

  explicit output_string_buffer(legacy_embedded_ctl::default_tracking_allocator_t<char> allocator,
                                strategy_t strategy = strategy_t::BEST_EFFORT_DICTIONARY);

  /**
   * adds a range to the list of result buffer ranges that are mapped to this char buffer
   */
  void add_result_buffer_range(common::aligned_blocked_range range) { result_buffer_ranges_.push_back(range); }

  /**
   * returns the actual used buffer size.
   */
  [[nodiscard]] size_t size() const { return buffer_.size(); }

  /**
   * returns the size of the best effort map
   */
  [[nodiscard]] size_t map_size() const { return best_effort_map_.size(); }

  /**
   * stores the given string into the buffer depending on the buffering strategy.
   */
  [[nodiscard]] char* store(std::string_view str);

  /**
   * reallocates the result buffer strings stored in the current char buffer to the passed target buffer and updates the
   * result buffer pointers to point to the target buffer.
   */
  void copy_to_given_continuous_buffer(legacy_embedded_ctl::array_view<char> target_char_buffer,
                                       legacy_embedded_ctl::array_view<cel_string_t> result_buffer);

 private:
  [[nodiscard]] std::pair<size_type, char*> copy_to_buffer(std::string_view str);

  void update_last_string(std::string_view str, size_type offset);

  /**
   * allocates a new buffer address and writes the given string to it.
   */
  [[nodiscard]] char* store_materialized(std::string_view str);

  /**
   * first checks the map for the given string. If the string exists, the found buffer offset is returned. Otherwise,
   * a new buffer address is allocated and the string is written to it. The offset of the newly allocated memory is then
   * returned.
   */
  [[nodiscard]] size_type store_dictified(std::string_view str);

  char_buffer buffer_;
  strategy_t strategy_;
  std::vector<common::aligned_blocked_range> result_buffer_ranges_{};
  std::optional<std::string_view> last_string_{std::nullopt};
  size_type last_buffer_offset_{0};
  ska::bytell_hash_map<std::string_view, size_type> best_effort_map_{};
};

}  // namespace celonis::accelerator::common