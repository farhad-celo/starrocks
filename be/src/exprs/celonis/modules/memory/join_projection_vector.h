#pragma once

#include <memory>
#include <variant>

#include "legacy_embedded_ctl/cache_fwd.h"
#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/memory/join_data_handler.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

class identity_join_projection {
 public:
  identity_join_projection() = default;
  explicit identity_join_projection(size_t size) : size_{size} {}

  [[nodiscard]] row_id operator[](row_id index) const {
    legacy_embedded_debug_assert(std::cmp_less(index, size()));
    return index;
  }
  [[nodiscard]] row_id at(row_id index) const {
    if (static_cast<size_t>(index) >= size()) [[unlikely]] {
      throw legacy_embedded_ctl::out_of_range{"Index [{}] is out of bounds for join projection vector of size [{}].",
                                              index, size()};
    }
    return operator[](index);
  }

  [[nodiscard]] size_t size() const noexcept { return size_; }

 private:
  size_t size_{0};
};

using join_projection32_t = legacy_embedded_ctl::shared_static_array<const join_32_t>;
using join_projection64_t = legacy_embedded_ctl::shared_static_array<const join_64_t>;

using join_projection_vector_t = std::variant<join_projection32_t, join_projection64_t>;
using pull_up_vector_t = std::variant<identity_join_projection, join_projection32_t, join_projection64_t>;

template <typename FUNCTION, typename... PROJECTION_VECTOR>
[[nodiscard]] decltype(auto) cast_execute_projection_vector(FUNCTION&& f, PROJECTION_VECTOR&&... projections) {
  return std::visit(legacy_embedded_ctl::overloaded{[&f](auto&&... projs) {
                      return std::invoke(std::forward<FUNCTION>(f), projs...);
                    }},
                    projections...);
}

[[nodiscard]] inline bool is_projection_vector_empty(const join_projection_vector_t& projection) {
  return cast_execute_projection_vector([](const auto& proj) { return proj.empty(); }, projection);
}

[[nodiscard]] inline size_t get_projection_vector_size(const join_projection_vector_t& projection) {
  return cast_execute_projection_vector([](const auto& proj) { return proj.size(); }, projection);
}

}  // namespace celonis::accelerator::memory

namespace celonis::accelerator::legacy_embedded_ctl {

template <>
struct is_cache_entry_unused<celonis::accelerator::memory::join_projection_vector_t> {
  [[nodiscard]] bool operator()(const celonis::accelerator::memory::join_projection_vector_t& value) const {
    // Less or equal one, since the cache owns an instance of the shared_ptr. So a use count of 1 means only the cache
    // still owns the value.
    return celonis::accelerator::memory::cast_execute_projection_vector(
        [](const auto& value) { return value.use_count() <= 1; }, value);
  }
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
