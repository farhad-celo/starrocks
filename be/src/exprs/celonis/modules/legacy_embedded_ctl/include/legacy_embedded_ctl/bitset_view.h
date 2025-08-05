#pragma once

#include <span>

#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/bit.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_types.h"
#include "legacy_embedded_ctl/bits/dynamic_bitset_utils.h"
#include "legacy_embedded_ctl/bitset_base.h"
#include "legacy_embedded_ctl/bitset_view_fwd.h"
#include "legacy_embedded_ctl/dynamic_bitset.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/exceptions.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief A bitset view is a non-mutable view of a bitset, unlike the dynamic_bitset which owns its own
 * underlying blocks, a bitset view has only a span of the underlying blocks, thus does not take any ownership.
 */
template <parallelism_settings_t PARALLELISM_SETTING>
class bitset_view : public bitset_crtp_base<PARALLELISM_SETTING, bitset_view<PARALLELISM_SETTING>> {
  friend class bitset_crtp_base<PARALLELISM_SETTING, bitset_view<PARALLELISM_SETTING>>;
  friend class bitset_mutable_view<PARALLELISM_SETTING>;

 public:
  using base_type = bitset_crtp_base<PARALLELISM_SETTING, bitset_view<PARALLELISM_SETTING>>;
  using value_type = details::bitset_types::value_type;
  using block_type = details::bitset_types::block_type<PARALLELISM_SETTING, value_type>;
  using size_type = details::bitset_types::size_type;

  bitset_view() = default;
  bitset_view(legacy_embedded_ctl::array_view<const block_type> view, size_t size) : view_{view}, size_{size} {
    common::runtime_assert(size_ <= view_.size() * base_type::BLOCK_SIZE,
                           "bitset_view: size {} does not match to the view with block size {}", size_, view_.size());
  }

  explicit bitset_view(const legacy_embedded_ctl::dynamic_bitset<PARALLELISM_SETTING>& bitset)
      : bitset_view(bitset.to_block_span(), bitset.size()) {}

  // for the view we do not provide a comparison operator, just like std::span
  [[nodiscard]] bool operator==(const bitset_view& rhs) const = delete;

  [[nodiscard]] size_t size() const noexcept { return size_; }

  /**
   * @brief get a non-mutable view of the underlying block array
   */
  [[nodiscard]] legacy_embedded_ctl::array_view<const block_type> to_block_span() const noexcept { return view_; }

 private:
  legacy_embedded_ctl::array_view<const block_type> view_{};
  size_type size_{0};
};

/**
 * @brief A bitset mutable view is a mutable view of a bitset.
 */
template <parallelism_settings_t PARALLELISM_SETTING>
class bitset_mutable_view : public bitset_crtp_base<PARALLELISM_SETTING, bitset_mutable_view<PARALLELISM_SETTING>> {
  friend class bitset_crtp_base<PARALLELISM_SETTING, bitset_mutable_view<PARALLELISM_SETTING>>;
  friend class bitset_view<PARALLELISM_SETTING>;

 public:
  using base_type = bitset_crtp_base<PARALLELISM_SETTING, bitset_mutable_view<PARALLELISM_SETTING>>;
  using value_type = details::bitset_types::value_type;
  using block_type = details::bitset_types::block_type<PARALLELISM_SETTING, value_type>;
  using size_type = details::bitset_types::size_type;

  bitset_mutable_view() = default;
  bitset_mutable_view(legacy_embedded_ctl::array_view<block_type> view, size_t size) : view_{view}, size_{size} {
    common::runtime_assert(size_ <= view_.size() * base_type::BLOCK_SIZE,
                           "bitset_mutable_view: size {} does not match to the view with block size {}", size_,
                           view_.size());
  }

  explicit bitset_mutable_view(legacy_embedded_ctl::dynamic_bitset<PARALLELISM_SETTING>& bitset)
      : bitset_mutable_view(bitset.to_mutable_block_span(), bitset.size()) {}

  // for the view we do not provide a comparison operator, just like std::span
  [[nodiscard]] bool operator==(const bitset_mutable_view& rhs) const = delete;

  [[nodiscard]] size_t size() const noexcept { return size_; }

  template <typename BITSET_VIEW>
  bitset_mutable_view& operator&=(const BITSET_VIEW& rhs) noexcept;

  template <typename BITSET_VIEW>
  bitset_mutable_view& operator|=(const BITSET_VIEW& rhs) noexcept;

  template <typename BITSET_VIEW>
  bitset_mutable_view& operator^=(const BITSET_VIEW& rhs) noexcept;

  /**
   * @brief get a non-mutable view of the underlying block array
   */
  [[nodiscard]] legacy_embedded_ctl::array_view<const block_type> to_block_span() const noexcept { return view_; }
  /**
   * @brief get a mutable view of the underlying block array
   */
  [[nodiscard]] legacy_embedded_ctl::array_view<block_type> to_mutable_block_span() noexcept { return view_; }

 private:
  legacy_embedded_ctl::array_view<block_type> view_{};
  size_type size_{0};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl