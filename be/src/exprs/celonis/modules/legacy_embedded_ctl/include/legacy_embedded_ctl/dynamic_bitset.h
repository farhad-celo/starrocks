#pragma once

#include <atomic>
#include <climits>
#include <type_traits>

#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bit.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_utils.h"
#include "legacy_embedded_ctl/bitset_base.h"
#include "legacy_embedded_ctl/dynamic_bitset_fwd.h"
#include "legacy_embedded_ctl/exception.h"
#include "legacy_embedded_ctl/static_array.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief The general idea for this bitset implementation is to have a drop-in replacement for boost::dynamic_bitset.
 * @description The motivation for this bitset implementation is due to two main reasons:
 * - 1.) Having an extended interface compared to a boost::dynamic_bitset (e.g., count(begin, end)) which enables us
 * to achieve a better performance in common bitset use cases
 * - 2.) Making parallel writes to the bitset thread safe by making use of std::atomic's in the internal block type
 * In order to make the bitset parallel-writable, use the
 * 'details::bitset_types::parallelism_setting::ENABLE_PARALLELISM' tag as template argument. By this, the internal
 * block type of the bitset implementation will be std::atomic<uint64_t>. When not providing the
 * 'details::bitset_types::parallelism_setting::ENABLE_PARALLELISM' tag or by providing the
 * 'details::bitset_types::parallelism_setting::DISABLE_PARALLELISM' tag, the bitset will use plain uint64_t as its
 * internal block type and is thus not suitable for parallel writes.
 * @tparam PARALLELISM_SETTING controls whether the bitset internals are such that it can work with parallel writes
 * @disclaimer Currently, several functionalities provided by boost::dynamic_bitset (such as shift operators or
 * resizing) are not provided in our custom implementation. If there is an actual need for these, consider creating a
 * PR/ticket for an interface extension.
 */
template <parallelism_settings_t PARALLELISM_SETTING>
class dynamic_bitset final : public bitset_crtp_base<PARALLELISM_SETTING, dynamic_bitset<PARALLELISM_SETTING>> {
 public:
  friend class bitset_crtp_base<PARALLELISM_SETTING, dynamic_bitset<PARALLELISM_SETTING>>;
  /**
   * @brief used to check whether the bitset instantiation has parallelism 'enabled'
   */
  using base_type = bitset_crtp_base<PARALLELISM_SETTING, dynamic_bitset<PARALLELISM_SETTING>>;
  using value_type = details::bitset_types::value_type;
  using block_type = details::bitset_types::block_type<PARALLELISM_SETTING, value_type>;
  using buffer_type = static_array<block_type>;
  using bit_index_type = details::bitset_types::bit_index_type;
  using block_index_type = details::bitset_types::block_index_type;
  using size_type = details::bitset_types::size_type;
  using allocator_type = details::bitset_types::allocator_type<PARALLELISM_SETTING>;

  /* constructors & destructor */
  dynamic_bitset() noexcept = default;
  explicit dynamic_bitset(size_type size, bool default_value = false);
  dynamic_bitset(size_type size, bool default_value, allocator_type allocator);
  dynamic_bitset(const dynamic_bitset& other);
  dynamic_bitset& operator=(const dynamic_bitset& other);
  dynamic_bitset(dynamic_bitset&& other) noexcept = default;
  dynamic_bitset& operator=(dynamic_bitset&& other) noexcept = default;
  ~dynamic_bitset() noexcept = default;

  /* bitset comparisons */
  /**
   * @brief Compares whether the two bitsets contain exactly the same bit patterns
   * @note Due to a GCC compiler bug we can not explicitly specify the 'noexcept' guarantees and 'default' the
   * implementation at the same time (probably related: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82099). We choose
   * to 'default' the implementation and don't specify the 'noexcept' spec. However, this does not (!) imply the
   * operator does throw. By 'default'ing the implementation, the 'noexcept'ness is derived by the compiler.
   */
  [[nodiscard]] bool operator==(const dynamic_bitset& rhs) const = default;  // compiler also provides operator!=

  /* basic bit operations and element access */
  /**
   * @brief bitwise-AND of all the bits in the bitset 'rhs' with the bits in this bitset
   * @param rhs the right-hand side bitset
   * @return a reference to this bitset
   * @note both bitsets (this and rhs) must have the same size
   */
  dynamic_bitset& operator&=(const dynamic_bitset& rhs) noexcept;
  [[nodiscard]] dynamic_bitset operator&(const dynamic_bitset& rhs) const;
  /**
   * @brief bitwise-OR of all the bits in the bitset 'rhs' with the bits in this bitset
   * @param rhs the right-hand side bitset
   * @return a reference to this bitset
   * @note both bitsets (this and rhs) must have the same size
   */
  dynamic_bitset& operator|=(const dynamic_bitset& rhs) noexcept;
  [[nodiscard]] dynamic_bitset operator|(const dynamic_bitset& rhs) const;
  /**
   * @brief bitwise-XOR of all the bits in the bitset 'rhs' with the bits in this bitset
   * @param rhs the right-hand side bitset
   * @return a reference to this bitset
   * @note both bitsets (this and rhs) must have the same size
   */
  dynamic_bitset& operator^=(const dynamic_bitset& rhs) noexcept;
  [[nodiscard]] dynamic_bitset operator^(const dynamic_bitset& rhs) const;

  /**
   * @brief get a non-mutable view of the underlying block array
   */
  [[nodiscard]] legacy_embedded_ctl::array_view<const block_type> to_block_span() const noexcept {
    return legacy_embedded_ctl::array_view{bitset_data_};
  }
  /**
   * @brief get a mutable view of the underlying block array
   */
  [[nodiscard]] legacy_embedded_ctl::array_view<block_type> to_mutable_block_span() noexcept { return legacy_embedded_ctl::array_view{bitset_data_}; }

  /* bitset size */
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] block_index_type num_blocks() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  template <parallelism_settings_t U>
  // NOLINTNEXTLINE(readability-redundant-declaration)
  friend void swap(dynamic_bitset<U>& lhs, dynamic_bitset<U>& rhs) noexcept;

 private:
  /* internal constructors */
  /**
   * @brief creates an unitialized bitset of the given size with the given allocator.
   */
  dynamic_bitset(size_type size, allocator_type allocator);

  /* data members */
  size_type size_{0};
  buffer_type bitset_data_{};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
