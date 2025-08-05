#pragma once

#include <compare>
#include <cstddef>
#include <numeric>
#include <span>

#include <bytell_hash_map.hpp>
#include <tbb/blocked_range.h>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/interval.h"
#include "legacy_embedded_ctl/memory/memory_fwd.h"
#include "legacy_embedded_ctl/static_array.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * A map from indices (starting at 0 up to a certain size) to ranges of elements.
 *
 * The number of indices and the number of elements is fixed.
 * To build the map either use the range_map_builder or the ordered_range_map_builder.
 */
template <typename IDX_TYPE, typename ELEMENT_TYPE>
class range_map {
  template <typename, typename>
  friend class resizable_range_map;

 public:
  using offsets_t = static_array<IDX_TYPE>;
  using elements_t = static_array<ELEMENT_TYPE>;
  using offsets_allocator_type = typename offsets_t::allocator_type;
  using elements_allocator_type = typename elements_t::allocator_type;

  /**
   * Creates a range_map from the given offsets and element array. For an index i, the associated range of elements
   * in the element array is given by the range [offset[i], offset[i+1]).
   */
  range_map(offsets_t&& offsets, elements_t&& elements) : offsets_{std::move(offsets)}, elements_{std::move(elements)} {
    abort_assert(!offsets_.empty() && offsets_[0] == 0 &&
                 static_cast<std::size_t>(offsets_[offsets_.size() - 1]) <= elements_.size());
  }

  [[nodiscard]] IDX_TYPE index_count() const noexcept { return static_cast<IDX_TYPE>(offsets_.size() - 1); }

  [[nodiscard]] IDX_TYPE element_count() const noexcept { return offsets_[offsets_.size() - 1]; }

  [[nodiscard]] IDX_TYPE element_capacity() const noexcept { return static_cast<IDX_TYPE>(elements_.size()); }

  [[nodiscard]] std::span<const ELEMENT_TYPE> get_span(IDX_TYPE idx) const noexcept {
    return std::span<const ELEMENT_TYPE>{elements_.begin() + offsets_[idx],
                                         elements_.begin() + offsets_[static_cast<IDX_TYPE>(idx + 1)]};
  }

  // Returns the begin of a span inside the elements array, so that it can be used as an index
  [[nodiscard]] IDX_TYPE get_span_begin(IDX_TYPE idx) const noexcept { return offsets_[idx]; }
  /*
   * returns a non const span, that can be sorted
   */
  [[nodiscard]] std::span<ELEMENT_TYPE> get_span(IDX_TYPE idx) noexcept {
    return std::span<ELEMENT_TYPE>{elements_.begin() + offsets_[idx],
                                   elements_.begin() + offsets_[static_cast<IDX_TYPE>(idx + 1)]};
  }

  [[nodiscard]] bool operator==(const range_map& rhs) const noexcept = default;

  [[nodiscard]] std::tuple<offsets_t, elements_t> release_data() && {
    return std::make_tuple(std::move(offsets_), std::move(elements_));
  }

  [[nodiscard]] std::pair<offsets_allocator_type, elements_allocator_type> get_allocators() {
    return {offsets_.get_allocator(), elements_.get_allocator()};
  }

 private:
  offsets_t offsets_;
  elements_t elements_;
};

/**
 * A resizable range maps wraps a normal range map and adds the functionality to resize individual ranges.
 * While ranges can be resized, they should remain non-overlapping.
 *
 * Effectively, this means that a range map can only be shrunk initially. However, the resized ranges in the map are
 * allowed to be enlarged as long as the ranges do not overlap.
 */
template <typename IDX_TYPE, typename ELEMENT_TYPE>
class resizable_range_map {
  using range_map_t = range_map<IDX_TYPE, ELEMENT_TYPE>;

 public:
  resizable_range_map(range_map_t&& map, const utils::allocation_reason& reason,
                      utils::allocation_priority priority = utils::allocation_priority::LOW)
      : ranges_end_{make_static_array_for_overwrite<IDX_TYPE>(map.index_count(), reason, priority)},
        map_(std::move(map)) {
    /** The end of a range is given by the offset of the next index. **/
    std::copy(std::next(map_.offsets_.begin()), map_.offsets_.end(), ranges_end_.begin());
  }

  resizable_range_map(range_map_t&& map, const utils::allocation_reason& reason,
                      typename range_map_t::elements_allocator_type allocator)
      : ranges_end_{make_static_array_for_overwrite<IDX_TYPE>(map.index_count(), reason, std::move(allocator))},
        map_(std::move(map)) {
    /** The end of a range is given by the offset of the next index. **/
    std::copy(std::next(map_.offsets_.begin()), map_.offsets_.end(), ranges_end_.begin());
  }

  // To preserve the interface of a range map
  [[nodiscard]] IDX_TYPE index_count() const noexcept { return map_.index_count(); }
  [[nodiscard]] IDX_TYPE element_count() const noexcept { return map_.element_count(); }
  [[nodiscard]] IDX_TYPE element_capacity() const noexcept { return map_.element_capacity(); }

  /**
   * Resizes a range with index idx. The offset can be used to discard elements at the front of the range, while the
   * new size can be used to discard elements at the end.
   *
   * @param idx The index of the range we want to resize.
   * @param new_size The number of elements in the resized range.
   * @param offset The offset at which we want the resized range to start.
   */
  void resize(IDX_TYPE idx, IDX_TYPE new_size, IDX_TYPE offset = 0) {
    if ((offset + new_size) > map_.offsets_.at(static_cast<IDX_TYPE>(idx + 1))) {
      throw std::range_error(
          fmt::format("Resizing the range of index {} with offset {} and new size {} exceeds "
                      "the bound of {} elements.",
                      idx, offset, new_size, map_.offsets_.at(static_cast<IDX_TYPE>(idx + 1))));
    }

    map_.offsets_.at(idx) += offset;
    ranges_end_.at(idx) = map_.offsets_.at(idx) + new_size;
  }

  [[nodiscard]] std::span<const ELEMENT_TYPE> get_span(IDX_TYPE idx) const noexcept {
    return std::span<const ELEMENT_TYPE>{map_.elements_.begin() + map_.offsets_.at(idx),
                                         map_.elements_.begin() + ranges_end_.at(idx)};
  }

  /*
   * returns a non const span, that can be sorted
   */
  [[nodiscard]] std::span<ELEMENT_TYPE> get_span(IDX_TYPE idx) noexcept {
    return std::span<ELEMENT_TYPE>{map_.elements_.begin() + map_.offsets_.at(idx),
                                   map_.elements_.begin() + ranges_end_.at(idx)};
  }

  [[nodiscard]] bool operator==(const resizable_range_map& rhs) const noexcept = default;

 private:
  static_array<IDX_TYPE> ranges_end_;
  range_map_t map_;
};

/**
 * TODO(s.griebel) Description and better integration with the other classes in this file
 */
template <typename IDX_TYPE = std::size_t>
class range_offsets {
 public:
  explicit range_offsets(static_array<IDX_TYPE>&& offsets) : offsets_{std::move(offsets)} {
    legacy_embedded_debug_assert(!offsets_.empty());
    legacy_embedded_debug_assert(offsets_[0] == 0);
  }

  [[nodiscard]] std::size_t index_count() const noexcept { return offsets_.size() - 1; }

  [[nodiscard]] std::size_t element_count() const noexcept { return offsets_.back(); }

  [[nodiscard]] half_open_interval<IDX_TYPE> range(IDX_TYPE idx) const {
    legacy_embedded_debug_assert(idx < index_count());
    return {range_begin(idx), range_end(idx)};
  }

  [[nodiscard]] IDX_TYPE range_begin(IDX_TYPE idx) const noexcept {
    legacy_embedded_debug_assert(idx < index_count());
    return offsets_[idx];
  }

  [[nodiscard]] IDX_TYPE range_end(IDX_TYPE idx) const noexcept {
    legacy_embedded_debug_assert(idx < index_count());
    return offsets_[idx + 1];
  }

 private:
  static_array<IDX_TYPE> offsets_;
};

/**
 * A histogram type that can be used to create an range_map_builder. For every index it counts how many elements there
 * should be in the final map.
 *
 * !! It is assumed that the sum of all counts for every index does fit into the index type !!
 *
 * This histogram is special because the array containing the counts has one more slot than required at the start,
 * which is useful for the generation of the offset array. This slot isn't accessible from the outside and doesn't count
 * for the size of the histogram.
 */
template <typename IDX_TYPE>
class range_map_histogram {
 public:
  using allocator_type = typename static_array<IDX_TYPE>::allocator_type;

  range_map_histogram(IDX_TYPE size, const utils::allocation_reason& reason,
                      utils::allocation_priority priority = utils::allocation_priority::LOW)
      : values_{make_static_array_value_init<IDX_TYPE>(static_cast<std::size_t>(size) + 1, reason, priority)} {}

  range_map_histogram(IDX_TYPE size, const utils::allocation_reason& reason, allocator_type allocator)
      : values_{
            make_static_array_value_init<IDX_TYPE>(static_cast<std::size_t>(size) + 1, reason, std::move(allocator))} {}

  explicit range_map_histogram(static_array<IDX_TYPE> values) : values_{std::move(values)} {}

  range_map_histogram copy() { return range_map_histogram<IDX_TYPE>{values_.copy()}; }

  void inc(std::size_t idx) noexcept { ++values_[idx + 1]; }

  void inc(std::size_t idx, IDX_TYPE n) noexcept { values_[idx + 1] += n; }

  [[nodiscard]] IDX_TYPE& operator[](std::size_t idx) noexcept { return values_[idx + 1]; }

  [[nodiscard]] IDX_TYPE operator[](std::size_t idx) const noexcept { return values_[idx + 1]; }

  /**
   * Creates an offset array from the histogram. For each index i the the value at position i in the offset vector
   * contains the number of elements before that index. Additionally the offset vector contains the total number of
   * elements at the last position.
   */
  [[nodiscard]] static_array<IDX_TYPE> to_offset_array() && {
    std::partial_sum(std::begin(values_), std::end(values_), std::begin(values_));
    return std::move(values_);
  }

  [[nodiscard]] IDX_TYPE index_count() const noexcept { return static_cast<IDX_TYPE>(values_.size() - 1); }

 private:
  static_array<IDX_TYPE> values_;
};

/**
 * This builder allocates space for each index according to the given range_map_histogram, which can then be filled
 * with elements. The size of the resulting ranges is determined by the histogram, so if not enough elements were
 * added to a certain index, the rest of the range is filled with the default_value provided in the constructor.
 */
template <typename IDX_TYPE, typename ELEMENT_TYPE>
class range_map_builder {
 public:
  using result_range_map_type = range_map<IDX_TYPE, ELEMENT_TYPE>;
  using offsets_allocator_type = typename result_range_map_type::offsets_allocator_type;
  using elements_allocator_type = typename result_range_map_type::elements_allocator_type;

  range_map_builder(range_map_histogram<IDX_TYPE>&& histogram, ELEMENT_TYPE default_value,
                    const utils::allocation_reason& reason,
                    utils::allocation_priority priority = utils::allocation_priority::LOW)
      : offsets_{histogram.to_offset_array()},
        values_{make_static_array(offsets_[offsets_.size() - 1], default_value, reason, priority)},
        filled_values_{make_static_array_value_init<IDX_TYPE>(offsets_.size() - 1, reason, priority)} {
    legacy_embedded_debug_assert(!offsets_.empty() && offsets_[0] == 0);
  }

  range_map_builder(range_map_histogram<IDX_TYPE>&& histogram, ELEMENT_TYPE default_value,
                    const utils::allocation_reason& reason, offsets_allocator_type idx_allocator,
                    elements_allocator_type element_allocator)
      : offsets_{histogram.to_offset_array()},
        values_{make_static_array(offsets_[offsets_.size() - 1], default_value, reason, std::move(element_allocator))},
        filled_values_{make_static_array_value_init<IDX_TYPE>(offsets_.size() - 1, reason, std::move(idx_allocator))} {
    legacy_embedded_debug_assert(!offsets_.empty() && offsets_[0] == 0);
  }

  range_map_builder(range_map_histogram<IDX_TYPE>&& histogram, const utils::allocation_reason& reason,
                    utils::allocation_priority priority = utils::allocation_priority::LOW)
      : offsets_{std::move(histogram).to_offset_array()},
        values_{make_static_array_for_overwrite<ELEMENT_TYPE>(offsets_[offsets_.size() - 1], reason, priority)},
        filled_values_{make_static_array_value_init<IDX_TYPE>(offsets_.size() - 1, reason, priority)} {
    legacy_embedded_debug_assert(!offsets_.empty() && offsets_[0] == 0);
  }

  range_map_builder(range_map_histogram<IDX_TYPE>&& histogram, const utils::allocation_reason& reason,
                    offsets_allocator_type idx_allocator, elements_allocator_type element_allocator)
      : offsets_{std::move(histogram).to_offset_array()},
        values_{make_static_array_for_overwrite<ELEMENT_TYPE>(offsets_[offsets_.size() - 1], reason,
                                                              std::move(element_allocator))},
        filled_values_{make_static_array_value_init<IDX_TYPE>(offsets_.size() - 1, reason, std::move(idx_allocator))} {
    legacy_embedded_debug_assert(!offsets_.empty() && offsets_[0] == 0);
  }

  void add_element(IDX_TYPE idx, ELEMENT_TYPE&& element) {
    legacy_embedded_debug_assert(static_cast<std::size_t>(idx) < offsets_.size() - 1);
    legacy_embedded_debug_assert(offsets_[idx] + filled_values_[idx] < offsets_[idx + 1]);
    // NOLINTNEXTLINE(bugprone-misplaced-widening-cast)
    values_[offsets_[idx] + filled_values_[idx]] = std::move(element);
    ++filled_values_[idx];
  }

  void add_element(IDX_TYPE idx, const ELEMENT_TYPE& element) {
    legacy_embedded_debug_assert(static_cast<std::size_t>(idx) < offsets_.size() - 1);
    legacy_embedded_debug_assert(offsets_[idx] + filled_values_[idx] < offsets_[idx + 1]);
    // NOLINTNEXTLINE(bugprone-misplaced-widening-cast)
    values_[offsets_[idx] + filled_values_[idx]] = element;
    ++filled_values_[idx];
  }

  [[nodiscard]] result_range_map_type to_range_map() && {
    filled_values_ = static_array<IDX_TYPE>();
    return result_range_map_type{std::move(offsets_), std::move(values_)};
  }

  [[nodiscard]] IDX_TYPE index_count() const noexcept { return static_cast<IDX_TYPE>(offsets_.size() - 1); }

 private:
  static_array<IDX_TYPE> offsets_;
  static_array<ELEMENT_TYPE> values_;
  static_array<IDX_TYPE> filled_values_;
};

/**
 * This builder can be used in cases where the elements of the range map can be added in the order of their associated
 * indices.
 *
 * After all elements for an index have been added, the function index_finished has to be called (also after the last
 * index).
 */
template <typename IDX_TYPE, typename ELEMENT_TYPE>
class ordered_range_map_builder {
 public:
  using result_range_map_type = range_map<IDX_TYPE, ELEMENT_TYPE>;
  using offsets_allocator_type = typename result_range_map_type::offsets_allocator_type;
  using elements_allocator_type = typename result_range_map_type::elements_allocator_type;

  /**
   * Constructs the builder with enough space for the given amount of indices and elements. index_finshed has to be
   * called as many times as there are indices. The number of elements can be an overapproximation.
   */
  ordered_range_map_builder(IDX_TYPE index_count, IDX_TYPE element_count, const utils::allocation_reason& reason,
                            utils::allocation_priority priority = utils::allocation_priority::LOW)
      : offsets_{make_static_array_value_init<IDX_TYPE>(static_cast<std::size_t>(index_count) + 1, reason, priority)},
        values_{
            make_static_array_for_overwrite<ELEMENT_TYPE>(static_cast<std::size_t>(element_count), reason, priority)} {}

  ordered_range_map_builder(IDX_TYPE index_count, IDX_TYPE element_count, const utils::allocation_reason& reason,
                            offsets_allocator_type idx_allocator, elements_allocator_type element_allocator)
      : offsets_{make_static_array_value_init<IDX_TYPE>(static_cast<std::size_t>(index_count) + 1, reason,
                                                        std::move(idx_allocator))},
        values_{make_static_array_for_overwrite<ELEMENT_TYPE>(static_cast<std::size_t>(element_count), reason,
                                                              std::move(element_allocator))} {}

  void add_element(ELEMENT_TYPE&& element) {
    legacy_embedded_debug_assert(static_cast<std::size_t>(curr_offset_) < values_.size());
    values_[curr_offset_] = std::move(element);
    ++curr_offset_;
  }

  void add_element(const ELEMENT_TYPE& element) {
    legacy_embedded_debug_assert(static_cast<std::size_t>(curr_offset_) < values_.size());
    values_[curr_offset_] = element;
    ++curr_offset_;
  }

  void index_finished() {
    ++curr_key_;
    offsets_[curr_key_] = curr_offset_;
  }

  [[nodiscard]] result_range_map_type to_range_map() && {
    abort_assert(static_cast<std::size_t>(curr_offset_) <= values_.size() &&
                 static_cast<std::size_t>(curr_key_) == offsets_.size() - 1);
    return result_range_map_type{std::move(offsets_), std::move(values_)};
  }

  [[nodiscard]] IDX_TYPE index_count() const noexcept { return static_cast<IDX_TYPE>(offsets_.size() - 1); }

 private:
  static_array<IDX_TYPE> offsets_;
  static_array<ELEMENT_TYPE> values_;
  IDX_TYPE curr_key_{0};
  IDX_TYPE curr_offset_{0};
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
