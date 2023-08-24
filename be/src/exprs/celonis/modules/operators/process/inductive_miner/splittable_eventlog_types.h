#pragma once

#ifdef CELOSTAR
#include <mutex>
#include <span>
#endif
#include <utility>
#include <variant>
#include <vector>

#ifndef CELOSTAR
#include "ctl/array_view.h"
#endif
#include "ctl/concepts.h"
#include "ctl/named_type.h"
#ifndef CELOSTAR
#include "ctl/static_array.h"
#endif
#include "ctl/type_sequence.h"
#include "ctl/type_traits.h"
#ifndef CELOSTAR
#include "modules/memory/column_pointers.h"
#endif
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process {

template <typename T>
concept compatible_with_row_id = ctl::standard_integer<T> && std::convertible_to<T, row_id>;

template <typename T>
using id_pair = std::pair<T, T>;

// TODO(n.weber): This could be moved to a much more central place and be reused in the rest of the code base
using activity_id_t = ctl::named_type<row_id, struct activity_id, ctl::comparable, ctl::hashable,
                                      ctl::implicitly_convertible_to<row_id>::templ>;
using trace_id_t = ctl::named_type<row_id, struct trace_id, ctl::comparable, ctl::hashable,
                                   ctl::implicitly_convertible_to<row_id>::templ>;

/**
 * @brief A pair of activity ID and trace ID integers. Both must be implicitly convertible to a row_id (the underlying
 * type for activity/trace IDs)
 */
template <compatible_with_row_id ID_TYPE>
struct activity_id_and_trace_id : public id_pair<ID_TYPE> {
  using value_type = ID_TYPE;
  using activity_id_raw_type = value_type;
  using trace_id_raw_type = value_type;
  using base = id_pair<value_type>;
  activity_id_and_trace_id() = default;
  activity_id_and_trace_id(const activity_id_raw_type activity_id, const trace_id_raw_type trace_id)
      : base{activity_id, trace_id} {}
  activity_id_and_trace_id(const activity_id_t activity_id, const trace_id_t trace_id)
      : base{activity_id.get(), trace_id.get()} {}
  /** Returns the non strongly-typed activity ID */
  [[nodiscard]] activity_id_raw_type activity_id_raw() const { return this->first; }
  /** Returns the activity ID as strong type */
  [[nodiscard]] activity_id_t activity_id() const { return activity_id_t{activity_id_raw()}; }
  /** Returns the non strongly-typed trace ID */
  [[nodiscard]] trace_id_raw_type trace_id_raw() const { return this->second; }
  /** Returns the trace ID as strong type */
  [[nodiscard]] trace_id_t trace_id() const { return trace_id_t{trace_id_raw()}; }
};

template <compatible_with_row_id ACTIVITY_ID_T, compatible_with_row_id TRACE_ID_T>
using activity_id_and_trace_id_t = activity_id_and_trace_id<ctl::max_integer_t<ACTIVITY_ID_T, TRACE_ID_T>>;

#ifdef CELOSTAR
template <typename TYPE>
class variant_multiplicities_t {
public:
  TYPE add_variant(size_t multiplicity) {
    std::lock_guard<std::mutex> guard(mu_);
    multiplicities_.push_back(multiplicity);
    return multiplicities_.size() - 1;
  }

  size_t get_multiplicity(TYPE trace_id) {
    std::lock_guard<std::mutex> guard(mu_);
    return multiplicities_[trace_id];
  }

private:
  std::vector<size_t> multiplicities_;
  std::mutex mu_;
};

template <typename TYPE>
struct shared_static_array_of_activity_id_and_trace_id {
  std::shared_ptr<activity_id_and_trace_id<TYPE>[]> buffer;
  size_t size;
  std::shared_ptr<variant_multiplicities_t<TYPE>> variant_multiplicities;

  bool operator==(const shared_static_array_of_activity_id_and_trace_id<TYPE>& rhs) const {
    if (size != rhs.size) {
      return false;
    }
    for (int i = 0; i < size; i++) {
      if (buffer[i] != rhs.buffer[i]) return false;
    }
    return true;
  }
};

template <typename TYPE>
struct as_static_array_of_activity_id_and_trace_id {
  using type = shared_static_array_of_activity_id_and_trace_id<TYPE>;
};

template <typename TYPE>
struct as_view_of_activity_id_and_trace_id {
  using type = std::span<activity_id_and_trace_id<TYPE>>;
};

using ptr_types = ctl::type_sequence<ctl::type_sequence<int32_t>>;
#else
template <typename TYPE>
struct as_static_array_of_activity_id_and_trace_id {
  using type = ctl::shared_static_array<activity_id_and_trace_id<TYPE>>;
};

template <typename TYPE>
struct as_view_of_activity_id_and_trace_id {
  using type = ctl::array_view<typename as_static_array_of_activity_id_and_trace_id<TYPE>::type::value_type>;
};

using ptr_types =
    ctl::type_sequence<ctl::type_sequence<memory::col_ptr_8_t>, ctl::type_sequence<memory::col_ptr_16_t>,
                       ctl::type_sequence<memory::col_ptr_32_t>, ctl::type_sequence<memory::col_ptr_64_t>>;
#endif

using eventlog_buffer_t = typename ctl::repack_types<
    std::variant, typename ctl::transform_types<ptr_types, as_static_array_of_activity_id_and_trace_id>::type>::type;

using eventlog_view_t = typename ctl::repack_types<
    std::variant, typename ctl::transform_types<ptr_types, as_view_of_activity_id_and_trace_id>::type>::type;

template <typename...>
struct eventlog_view_element;

template <typename ID_T>
struct eventlog_view_element<activity_id_and_trace_id<ID_T>> {
  using type = activity_id_and_trace_id<ID_T>;
};

#ifdef CELOSTAR
template <typename T>
struct eventlog_view_element<std::span<T>> : eventlog_view_element<T> {};
#else
template <typename T>
struct eventlog_view_element<ctl::array_view<T>> : eventlog_view_element<T> {};
#endif

template <typename... Ts>
using eventlog_view_element_t = typename eventlog_view_element<Ts...>::type;

enum class pick_activity_or_trace_id { ACTIVITY_ID, TRACE_ID };
static constexpr auto PICK_ACTIVITY_ID{pick_activity_or_trace_id::ACTIVITY_ID};
static constexpr auto PICK_TRACE_ID{pick_activity_or_trace_id::TRACE_ID};
static constexpr auto PICK_CASE_ID{PICK_TRACE_ID};     // Alias for the eventlog based IM case
static constexpr auto PICK_VARIANT_ID{PICK_TRACE_ID};  // Alias for the variant based IM case

// TODO(a.swoboda) Revisit with the next clang(-tidy/-format) update. It should be fixed with clang-16.
// This is a crutch: It would be better to just use a std::ranges::element_view or std::ranges::transform_view, but,
// alas, those are not usable with the current version of clang
template <pick_activity_or_trace_id ACTIVITY_OR_TRACE_ID, ctl::contiguous_sized_range RANGE>
struct eventlog_element final {
  RANGE range;
  [[nodiscard]] auto operator[](size_t i) const noexcept {
    if constexpr (ACTIVITY_OR_TRACE_ID == PICK_ACTIVITY_ID) {
      return range[i].activity_id().get();
    } else if constexpr (ACTIVITY_OR_TRACE_ID == PICK_TRACE_ID) {
      return range[i].trace_id().get();
    } else {
      static_assert(ctl::always_false_v<decltype(ACTIVITY_OR_TRACE_ID)>);
    }
  }
  [[nodiscard]] auto size() const noexcept { return range.size(); }
};

template <pick_activity_or_trace_id ACTIVITY_OR_TRACE_ID, ctl::contiguous_sized_range RANGE>
[[nodiscard]] auto element(RANGE range) noexcept {
  return eventlog_element<ACTIVITY_OR_TRACE_ID, RANGE>{std::move(range)};
};

using split_mapping_t = std::vector<size_t>;

using activity_domain_count_t = ctl::named_type<row_id, struct activity_domain_count_tag, ctl::comparable,
                                                ctl::implicitly_convertible_to<row_id>::templ>;
/** Strong type representing the number of 'traces' (represents either a case or variant) in the splittable eventlog. */
using trace_domain_count_t = ctl::named_type<row_id, struct trace_domain_count_tag, ctl::comparable,
                                             ctl::implicitly_convertible_to<row_id>::templ>;

}  // namespace celonis::accelerator::operators::process
