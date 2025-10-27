#pragma once

#include <memory>
#include <variant>

#include "legacy_embedded_ctl/static_array_fwd.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "legacy_embedded_ctl/utility.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/management/swap_info_fwd.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory {

using join_32_t = int32_t;
using join_64_t = int64_t;

using raw_join32_t = legacy_embedded_ctl::shared_static_array<join_32_t>;
using raw_join64_t = legacy_embedded_ctl::shared_static_array<join_64_t>;

using join_raw_t = std::variant<raw_join64_t, raw_join32_t>;

[[nodiscard]] join_raw_t create_raw_join(row_id fact_table_size, row_id dim_table_size, zero_init_t initialize_to_0,
                                         const common::execution_context& context);

using join_data_handler32_t = management::raw_data_handler_t<join_32_t>;
using join_data_handler64_t = management::raw_data_handler_t<join_64_t>;

using join_data_handler_t = std::variant<join_data_handler64_t, join_data_handler32_t>;

#ifndef CELOSTAR
[[nodiscard]] std::optional<join_data_handler_t> create_join_from_swap(const std::string& swap_file,
                                                                       const management::swap_info& sinfo,
                                                                       const std::string& description);
#endif

template <typename JOIN_TYPE>
[[nodiscard]] management::raw_data_handler_t<JOIN_TYPE> create_join_from_raw_data(
    const legacy_embedded_ctl::shared_static_array<JOIN_TYPE>& raw_join, const std::string& file_name,
    const std::string& description, const management::swap_info& sinfo) {
  legacy_embedded_debug_assert(std::is_same_v<decltype(raw_join), const raw_join64_t&> ||
                               std::is_same_v<decltype(raw_join), const raw_join32_t&>);
  return management::raw_data_handler<JOIN_TYPE>::create_data_handler(raw_join, file_name, sinfo, description);
}

[[nodiscard]] join_data_handler_t create_join_from_raw_data(const join_raw_t& raw_join, const std::string& file_name,
                                                            const std::string& description,
                                                            const management::swap_info& sinfo);

template <typename T>
concept join_concept =
    std::is_same_v<std::remove_cvref_t<T>, join_raw_t> || std::is_same_v<std::remove_cvref_t<T>, join_data_handler_t>;

template <typename FUNCTION, join_concept... JOIN>
[[nodiscard]] decltype(auto) cast_execute_join(FUNCTION&& f, JOIN&&... joins) {
  return std::visit(legacy_embedded_ctl::overloaded{[&f](auto&&... joins) {
                      return std::invoke(std::forward<FUNCTION>(f), joins...);
                    }},
                    joins...);
}

[[nodiscard]] size_t get_join_vector_size(const join_data_handler_t& join);

[[nodiscard]] size_t get_join_vector_size(const join_raw_t& join);

}  // namespace celonis::accelerator::memory
