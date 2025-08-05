#include "output_string_buffer.h"

#include <tbb/parallel_for.h>

namespace celonis::accelerator::common {

output_string_buffer::output_string_buffer(legacy_embedded_ctl::default_tracking_allocator_t<char> allocator, strategy_t strategy)
    : buffer_{char_buffer{std::move(allocator)}}, strategy_{strategy} {}

std::pair<output_string_buffer::size_type, char*> output_string_buffer::copy_to_buffer(std::string_view str) {
  const size_t str_len_with_null{str.size() + 1};
  auto [buffer_offset, char_buffer_ptr]{buffer_.request_with_offset(str_len_with_null)};
  std::copy_n(str.data(), str_len_with_null, char_buffer_ptr);
  return {buffer_offset, char_buffer_ptr};
}

void output_string_buffer::update_last_string(std::string_view str, output_string_buffer::size_type offset) {
  last_string_ = str;
  last_buffer_offset_ = offset;
}

char* output_string_buffer::store_materialized(std::string_view str) { return copy_to_buffer(str).second; }

output_string_buffer::size_type output_string_buffer::store_dictified(std::string_view str) {
  // check if we can reuse the last string
  if (last_string_.has_value() && str == last_string_) {
    return last_buffer_offset_;
  }

  if (best_effort_map_.size() >= MAP_CAPACITY) {
    if (auto offset_itr{best_effort_map_.find(str)}; offset_itr != best_effort_map_.end()) {
      return offset_itr->second;
    }
    const auto [buffer_offset, char_buffer_ptr]{copy_to_buffer(str)};
    update_last_string(char_buffer_ptr, buffer_offset);

    return buffer_offset;
  }

  // try to insert into map
  auto [insert_itr, inserted]{best_effort_map_.emplace(str, 0)};
  if (!inserted) {
    update_last_string(insert_itr->first, insert_itr->second);
    return insert_itr->second;
  }

  const auto [buffer_offset, char_buffer_ptr]{copy_to_buffer(str)};
  // overwrite the temporary values with the correct pointers/offsets in the buffer
  insert_itr->first = char_buffer_ptr;
  insert_itr->second = buffer_offset;
  update_last_string(char_buffer_ptr, buffer_offset);

  return buffer_offset;
}

char* output_string_buffer::store(std::string_view str) {
  switch (strategy_) {
    case strategy_t::MATERIALIZED:
      return store_materialized(str);
    case strategy_t::BEST_EFFORT_DICTIONARY:
      // This is actually an offset into the temporary thread local char buffer.
      // we correct it below when copying the strings to the continuous output char buffer, because at that point we
      // have the final base address. NOLINTNEXTLINE(performance-no-int-to-ptr)
      return reinterpret_cast<char*>(store_dictified(str));
  }
  throw common::internal_exception{"Unknown output string buffer strategy"};
}

void output_string_buffer::copy_to_given_continuous_buffer(legacy_embedded_ctl::array_view<char> target_char_buffer,
                                                           legacy_embedded_ctl::array_view<cel_string_t> result_buffer) {
  common::runtime_assert(strategy_ == strategy_t::BEST_EFFORT_DICTIONARY, "Runtime Assertion failed");
  // reallocate_temporary buffer to correct location in result buffer
  buffer_.copy_to_given_continuous_buffer_and_reset(target_char_buffer);
  // update the result buffer pointers
  const auto* raw_target_char_buffer{target_char_buffer.begin()};

  tbb::parallel_for(static_cast<size_t>(0), result_buffer_ranges_.size(),
                    [&result_buffer_ranges = std::as_const(result_buffer_ranges_), &result_buffer,
                     &raw_target_char_buffer](size_t k) {
                      const auto& range{result_buffer_ranges[k]};
                      for (auto i = range.begin; i < range.end; i++) {
                        // Add the base pointer address to the offsets we added earlier.
                        result_buffer[i] += reinterpret_cast<ptrdiff_t>(raw_target_char_buffer);
                      }
                    });
}

}  // namespace celonis::accelerator::common