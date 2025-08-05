#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <functional>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <type_traits>
#include <utility>

#include <bytell_hash_map.hpp>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bitset_view.h"
#include "legacy_embedded_ctl/hash.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "modules/memory/transform/dictifier_types.h"

namespace celonis::accelerator::memory::transform::details {

template <typename T>
struct hash_key {
 public:
  hash_key() = default;
  explicit hash_key(T data) : value_{data} {
    if constexpr (std::is_same_v<T, cel_float_t>) {
      normalize_zero_value();
    }
  };

  [[nodiscard]] const T& value() const { return value_; };

  [[nodiscard]] size_t size() const { return sizeof(value_); }

  // NOLINTNEXTLINE(modernize-use-nullptr)
  [[nodiscard]] auto operator<=>(const hash_key<T>&) const = default;

  using hash = std::hash<T>;

 private:
  T value_;

  inline void normalize_zero_value() {
    if (std::fpclassify(value_) == FP_ZERO) {
      value_ = std::fabs(value_);
    }
  }
};

template <>
struct hash_key<cel_string_t> {
 public:
  hash_key() = default;
  explicit hash_key(cel_string_t data) : value_{data} {};

  [[nodiscard]] const cel_string_key& value() const { return value_; }

  [[nodiscard]] size_t size() const { return value_.str_len_with_null_byte(); }

  // NOLINTNEXTLINE(modernize-use-nullptr)
  [[nodiscard]] auto operator<=>(const hash_key<cel_string_t>&) const = default;

  struct hash {
    [[nodiscard]] size_t operator()(const cel_string_key& data) const {
      return legacy_embedded_ctl::hash_murmur_64a(data.str_without_null_byte());
    }
  };

 private:
  cel_string_key value_;
};

template <typename KEY_T, typename HASHER_T = typename hash_key<KEY_T>::hash>
class parallel_hash_table {
 public:
  // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
  // TODO(j.boettcher) clang tidy bug with unions fixed in https://reviews.llvm.org/D127293.
  // Should work with clang-tidy 15 or 16

  struct hash_entry {
   public:
    hash_key<KEY_T> key{};

    union {
      // In the first stage of dicitfy we need the next* to handle hash collisions
      // Later, when we sort the entries, we store the dictionary id into the value field
      hash_entry* next{nullptr};
      uint64_t value;
    };

    void init(KEY_T k) {
      key = hash_key<KEY_T>{k};
      next = nullptr;
    }
  };
  // NOLINTEND(cppcoreguidelines-pro-type-member-init)

  class hash_entry_buffer {
    static constexpr size_t BUFFER_SIZE{1024};

   public:
    explicit hash_entry_buffer(const common::execution_context& context)
        : entries_buffer_{memory::tracking::make_static_array_value_init<hash_entry>(
              BUFFER_SIZE, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)}

    {}
    // Do not allow copy construction
    hash_entry_buffer() = default;
    hash_entry_buffer(hash_entry_buffer&&) noexcept = default;
    hash_entry_buffer(const hash_entry_buffer&) = delete;
    hash_entry_buffer& operator=(hash_entry_buffer&&) = delete;
    hash_entry_buffer& operator=(const hash_entry_buffer&) = delete;

    [[nodiscard]] hash_entry* get_unused_entry(const KEY_T key) {
      legacy_embedded_debug_assert(!is_full());
      auto& entry{entries_buffer_.at(next_free_entry_++)};
      entry.init(key);
      return &entry;
    }

    // Makes the last entry available in the buffer again
    void reclaim_last_entry(hash_entry* entry) {
      legacy_embedded_debug_assert(next_free_entry_ > 0);
      legacy_embedded_debug_assert(entry == &entries_buffer_[next_free_entry_ - 1], "only the last entry can be reclaimed");
      next_free_entry_--;
    }

    auto begin() { return entries_buffer_.begin(); }
    auto end() { return entries_buffer_.begin() + size(); }
    size_t size() { return next_free_entry_; }
    bool is_full() { return size() == BUFFER_SIZE; }

   private:
    legacy_embedded_ctl::static_array<hash_entry> entries_buffer_;
    size_t next_free_entry_{0};
  };

  parallel_hash_table(size_t expected_distinct_count, const common::execution_context& context,
                      size_t grain_size = 1024, HASHER_T hasher = HASHER_T{})
      : size_{pick_hash_table_size(expected_distinct_count)},
        num_slots_minus_one_{size_ - 1},
        context_(context),
        hasher_{hasher},
        directory_{memory::tracking::make_static_array<std::atomic<hash_entry*>>(
            size_, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)},
        grain_size_{grain_size} {}

  /**
   * @brief inserts all input elements and returns an array that maps every element of the input to a unique hash_entry.
   * Elements with the same key point to the same hash_entry.
   */
  legacy_embedded_ctl::static_array<hash_entry*> batch_insert_or_get(std::span<const KEY_T> keys, const legacy_embedded_ctl::bitset_view_t null_flags,
                                                     const uint64_t max_num_hash_collisions_per_bucket = 100) {
    auto associated_entries{memory::tracking::make_static_array_for_overwrite<hash_entry*>(
        keys.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context_)};

    tbb::enumerable_thread_specific<std::vector<hash_entry_buffer>> tls_entry_buffers;
    tbb::parallel_for(
        tbb::blocked_range<row_id>{0, static_cast<row_id>(keys.size()), grain_size_}, [&](const auto& range) {
          size_t local_insert_count{0};
          size_t local_buffer_size{0};

          bool exists{false};
          auto& buffers = tls_entry_buffers.local(exists);
          if (!exists) {
            buffers.emplace_back(context_);
          }

          null_flags.apply_in_range([&associated_entries](size_t row) { associated_entries[row] = nullptr; },
                                    range.begin(), range.end());

          null_flags.apply_on_unset_in_range(
              [this, &associated_entries, &buffers, &local_buffer_size, &local_insert_count, &keys,
               max_num_hash_collisions_per_bucket](size_t row) {
                auto* buffer{&buffers.back()};

                if (buffer->is_full()) [[unlikely]] {
                  // Buffer is full: create a new one
                  buffer = &buffers.emplace_back(context_);
                }
                auto* entry{buffer->get_unused_entry(keys[row])};
                auto [final_entry, num_collisions] = this->insert_or_get_internal(entry);

                if (num_collisions > max_num_hash_collisions_per_bucket) {
                  throw common::internal_exception{
                      "Exceeded the allowed number of collisions ({}) in the parallel hash table",
                      max_num_hash_collisions_per_bucket};
                }

                if (entry == final_entry) {
                  // we did succeed in inserting the entry
                  local_insert_count++;
                  local_buffer_size += entry->key.size();
                } else {
                  // We did not use the last entry, put it back in our buffer
                  buffer->reclaim_last_entry(entry);
                }

                associated_entries[row] = final_entry;
              },
              range.begin(), range.end());

          distinct_item_count_ += local_insert_count;
          total_buffer_size_ += local_buffer_size;
        });

    for (auto& buffer_vecs : tls_entry_buffers) {
      this->add_hash_entry_buffers(std::move(buffer_vecs));
      buffer_vecs.clear();
    }

    return associated_entries;
  }

  [[nodiscard]] hash_entry* get(KEY_T data) const {
    const hash_key<KEY_T> key{data};
    const auto hash{hasher_(key.value())};
    const auto index{hash_policy_.index_for_hash(hash, num_slots_minus_one_)};

    auto& bucket{directory_[index]};
    auto entry{bucket.load()};

    while (entry != nullptr) {
      if (entry->key == key) {
        return entry;
      }
      entry = entry->next;
    }

    return nullptr;
  }

  legacy_embedded_ctl::static_array<hash_entry*> get_entries() {
    auto entries{memory::tracking::make_static_array_for_overwrite<hash_entry*>(
        distinct_item_count_, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context_)};

    std::atomic_size_t index{0};

    tbb::parallel_for_each(entry_buffers_.begin(), entry_buffers_.end(), [&entries, &index](auto& buffer) {
      size_t local_index{index.fetch_add(buffer.size())};

      for (auto& entry : buffer) {
        entries[local_index] = &entry;
        local_index++;
      }
    });

    legacy_embedded_debug_assert(entries.size() == index);
    return entries;
  }

  [[nodiscard]] size_t get_total_buffer_size() const { return total_buffer_size_; }

  [[nodiscard]] size_t get_distinct_item_count() const { return distinct_item_count_; }

 private:
  ska::fibonacci_hash_policy hash_policy_;
  const size_t size_;
  const size_t num_slots_minus_one_;
  const common::execution_context& context_;
  [[no_unique_address]] const HASHER_T hasher_;

  legacy_embedded_ctl::static_array<std::atomic<hash_entry*>> directory_;
  std::vector<hash_entry_buffer> entry_buffers_;
  hash_entry null_string_entry;

  std::atomic_size_t total_buffer_size_{0};
  std::atomic_size_t distinct_item_count_{0};

  const size_t grain_size_;

  void add_hash_entry_buffers(std::vector<hash_entry_buffer>&& buffer) {
    // N.B. this call is not synchronized and must not be called concurrently!
    std::move(buffer.begin(), buffer.end(), std::back_inserter(entry_buffers_));
  }

  std::pair<hash_entry*, uint64_t> insert_or_get_internal(hash_entry* entry) {
    const auto hash{hasher_(entry->key.value())};
    const auto index{hash_policy_.index_for_hash(hash, num_slots_minus_one_)};

    auto& bucket{directory_[index]};
    // Remember the first element
    auto expected_first = bucket.load();
    hash_entry* stop_entry{nullptr};

    uint64_t num_collisions{0};

    do {
      entry->next = expected_first;
      // Check if the value is already in the chain
      auto* prev_entry = expected_first;
      while (prev_entry != stop_entry) {
        num_collisions++;
        if (entry->key == prev_entry->key) {
          return {prev_entry, num_collisions};  // Value was already present; return the existing pointer.
        }
        prev_entry = prev_entry->next;
      }

      // Remember until which position we have checked the list for duplicates so far.
      stop_entry = expected_first;

      // Element is not part of the dictionary yet
      // Insert if the first element was not changed meanwhile
      // Otherwise we must repeat the procedure as we must not miss concurrent inserts
    } while (!bucket.compare_exchange_weak(expected_first, entry));

    // Insert succeeded
    return {entry, num_collisions};
  }

  size_t pick_hash_table_size(size_t num_elements) {
    auto hash_index = hash_policy_.next_size_over(num_elements);
    hash_policy_.commit(hash_index);
    legacy_embedded_debug_assert(num_elements > 0, "the hash table must have at least a bucket");
    return num_elements;
  }
};

}  // namespace celonis::accelerator::memory::transform::details
