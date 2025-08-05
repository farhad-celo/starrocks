#pragma once

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/named_type.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "modules/common/int_types.h"
#include "modules/memory/table_meta_data_fwd.h"

namespace celonis::accelerator {

// forward declare for proto TableMetaData in queries.pb.h
class TableMetaData;

namespace memory {

#ifndef CELOSTAR
/**
 * @brief Transforms a 'table_meta_data' instance to its equivalent proto representation 'TableMetaData'
 */
void table_meta_data_to_proto(TableMetaData* table_meta_data_proto, const table_meta_data& meta_data);
#endif

/**
 * @brief Represents meta data of a table:
 * - is the table visible (i.e., non-internal)
 * - is the table generated (e.g., by an operator) or was it loaded (e.g., from the data model)
 * - what is the type of the table (e.g., data model, query scope, augmentation, etc.)
 * @note Keep class in sync. with equivalent in TableMetaData.java and queries.proto
 */
class table_meta_data final {
 public:
  enum table_type {
    INVALID,
    // regular data model table
    DATA_MODEL_TABLE,
    // table in the data model for user authentication
    USER_AUTHENTICATION_TABLE,
    // lives only as long as the query scope is not out of scope
    QUERY_SCOPE_TABLE,
    QUERY_SCOPE_AGGREGATION_TABLE,
    QUERY_SCOPE_RESULT_TABLE,
    // operator generated tables not usable in PQL
    OPERATOR_TABLE,
    // load generated tables usable in PQL by their table name (e.g., case table)
    LOAD_GENERATED,
    // load generated automerge table
    LOAD_GENERATED_AUTO_MERGE,
    // OBJECT_LINK tables generated at load. Not usable in PQL
    LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE,
    LOAD_GENERATED_SIGNAL_LINK_EDGE,
    // augmentation tables
    AUGMENTATION_TABLE,
    // system catalog tables
    SYSTEM_CATALOG_TABLE
  };

  enum table_system_catalog_type { NO_CATALOG_TABLE, JOIN_TABLE };

  /** getters */
  [[nodiscard]] constexpr bool is_user_visible() const noexcept;
  [[nodiscard]] constexpr bool is_generated() const noexcept;
  [[nodiscard]] constexpr bool is_system_table() const noexcept;
  [[nodiscard]] constexpr table_type get_table_type() const noexcept;
  [[nodiscard]] constexpr bool is_data_model_table() const noexcept;
  [[nodiscard]] constexpr bool is_user_authentication_table() const noexcept;
  [[nodiscard]] constexpr bool is_query_scope_table() const noexcept;
  [[nodiscard]] constexpr bool is_query_scope_aggregation_table() const noexcept;
  [[nodiscard]] constexpr bool is_query_scope_result_table() const noexcept;
  [[nodiscard]] constexpr bool is_operator_table() const noexcept;
  [[nodiscard]] constexpr bool is_load_generated_table() const noexcept;
  [[nodiscard]] constexpr bool is_load_generated_auto_merge_table() const noexcept;
  [[nodiscard]] constexpr bool is_load_generated_object_link_intermediate_table() const noexcept;
  [[nodiscard]] constexpr bool is_load_generated_object_link_edge_table() const noexcept;
  [[nodiscard]] constexpr bool is_augmentation_table() const noexcept;
  [[nodiscard]] constexpr bool is_system_catalog_table() const noexcept;

  /**
   * @brief Returns true iff the table is a system catalog table and contains the join relationships
   */
  [[nodiscard]] constexpr bool is_system_catalog_join_table() const noexcept;

  /** factories */
  /**
   * @brief Table meta data factory which produces a correct 'table_meta_data' instance for the given table type
   * @tparam TABLE_TYPE the table type for which to produce the 'table_meta_data' instance
   * @return a 'table_meta_data' instance which has set its internals according to the given table type enum value
   */
  template <table_type TABLE_TYPE>
  [[nodiscard]] static constexpr table_meta_data make_for_type() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_data_model_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_user_authentication_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_query_scope_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_query_scope_aggregation_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_query_scope_result_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_operator_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_load_generated_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_load_generated_auto_merge_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_load_generated_object_link_intermediate_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_load_generated_object_link_edge_table() noexcept;
  [[nodiscard]] static constexpr table_meta_data make_for_augmentation_table() noexcept;

  [[nodiscard]] static constexpr table_meta_data make_for_system_catalog_join_table() noexcept;

 private:
  using user_visible_t = legacy_embedded_ctl::named_type<bool, struct user_visible_tag, legacy_embedded_ctl::implicitly_convertible_to<bool>::templ>;
  using generated_t = legacy_embedded_ctl::named_type<bool, struct generated_tag, legacy_embedded_ctl::implicitly_convertible_to<bool>::templ>;
  using system_table_t = legacy_embedded_ctl::named_type<bool, struct system_table_t, legacy_embedded_ctl::implicitly_convertible_to<bool>::templ>;

  constexpr table_meta_data(user_visible_t is_visible, generated_t is_generated, system_table_t is_system_table,
                            table_type table_type,
                            table_system_catalog_type system_catalog_type = NO_CATALOG_TABLE) noexcept;

  user_visible_t is_user_visible_{false};
  generated_t is_generated_{false};
  system_table_t is_system_table_{false};
  table_type table_type_{INVALID};
  table_system_catalog_type system_catalog_type_{NO_CATALOG_TABLE};
};

constexpr table_meta_data::table_meta_data(const user_visible_t is_visible, const generated_t is_generated,
                                           const system_table_t is_system_table, const table_type table_type,
                                           table_system_catalog_type system_catalog_type) noexcept
    : is_user_visible_{is_visible},
      is_generated_{is_generated},
      is_system_table_{is_system_table},
      table_type_{table_type},
      system_catalog_type_{system_catalog_type} {}

constexpr bool table_meta_data::is_user_visible() const noexcept { return is_user_visible_.get(); }

constexpr bool table_meta_data::is_generated() const noexcept { return is_generated_.get(); }

constexpr bool table_meta_data::is_system_table() const noexcept { return is_system_table_.get(); }

constexpr table_meta_data::table_type table_meta_data::get_table_type() const noexcept { return table_type_; }

constexpr bool table_meta_data::is_data_model_table() const noexcept {
  return get_table_type() == table_type::DATA_MODEL_TABLE;
}

constexpr bool table_meta_data::is_user_authentication_table() const noexcept {
  return get_table_type() == table_type::USER_AUTHENTICATION_TABLE;
}

constexpr bool table_meta_data::is_query_scope_table() const noexcept {
  return get_table_type() == table_type::QUERY_SCOPE_TABLE;
}

constexpr bool table_meta_data::is_query_scope_aggregation_table() const noexcept {
  return get_table_type() == table_type::QUERY_SCOPE_AGGREGATION_TABLE;
}

constexpr bool table_meta_data::is_query_scope_result_table() const noexcept {
  return get_table_type() == table_type::QUERY_SCOPE_RESULT_TABLE;
}

constexpr bool table_meta_data::is_operator_table() const noexcept {
  return get_table_type() == table_type::OPERATOR_TABLE;
}

constexpr bool table_meta_data::is_load_generated_table() const noexcept {
  return get_table_type() == table_type::LOAD_GENERATED;
}

constexpr bool table_meta_data::is_load_generated_auto_merge_table() const noexcept {
  return get_table_type() == table_type::LOAD_GENERATED_AUTO_MERGE;
}

constexpr bool table_meta_data::is_load_generated_object_link_intermediate_table() const noexcept {
  return get_table_type() == table_type::LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE;
}

constexpr bool table_meta_data::is_load_generated_object_link_edge_table() const noexcept {
  return get_table_type() == table_type::LOAD_GENERATED_SIGNAL_LINK_EDGE;
}

constexpr bool table_meta_data::is_augmentation_table() const noexcept {
  return get_table_type() == table_type::AUGMENTATION_TABLE;
}

constexpr bool table_meta_data::is_system_catalog_table() const noexcept {
  return get_table_type() == table_type::SYSTEM_CATALOG_TABLE;
}

constexpr bool table_meta_data::is_system_catalog_join_table() const noexcept {
  return system_catalog_type_ == table_system_catalog_type::JOIN_TABLE;
}

template <table_meta_data::table_type TABLE_TYPE>
constexpr table_meta_data table_meta_data::make_for_type() noexcept {
  if constexpr (TABLE_TYPE == table_meta_data::table_type::DATA_MODEL_TABLE) {
    return make_for_data_model_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::USER_AUTHENTICATION_TABLE) {
    return make_for_user_authentication_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::QUERY_SCOPE_TABLE) {
    return make_for_query_scope_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::QUERY_SCOPE_AGGREGATION_TABLE) {
    return make_for_query_scope_aggregation_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::QUERY_SCOPE_RESULT_TABLE) {
    return make_for_query_scope_result_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::OPERATOR_TABLE) {
    return make_for_operator_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::LOAD_GENERATED) {
    return make_for_load_generated_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::LOAD_GENERATED_AUTO_MERGE) {
    return make_for_load_generated_auto_merge_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE) {
    return make_for_load_generated_object_link_intermediate_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::LOAD_GENERATED_SIGNAL_LINK_EDGE) {
    return make_for_load_generated_object_link_edge_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::AUGMENTATION_TABLE) {
    return make_for_augmentation_table();
  } else if constexpr (TABLE_TYPE == table_meta_data::table_type::SYSTEM_CATALOG_TABLE) {
    static_assert(legacy_embedded_ctl::always_false_v<decltype(TABLE_TYPE)>, "No factory function for SYSTEM_CATALOG_TABLE exists.");
    legacy_embedded_ctl::assert_unreachable();
  }
}

constexpr table_meta_data table_meta_data::make_for_data_model_table() noexcept {
  return table_meta_data{user_visible_t{true}, generated_t{false}, system_table_t{false},
                         table_meta_data::table_type::DATA_MODEL_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_user_authentication_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{false}, system_table_t{false},
                         table_meta_data::table_type::USER_AUTHENTICATION_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_query_scope_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::QUERY_SCOPE_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_query_scope_aggregation_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::QUERY_SCOPE_AGGREGATION_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_query_scope_result_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::QUERY_SCOPE_RESULT_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_operator_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::OPERATOR_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_load_generated_table() noexcept {
  return table_meta_data{user_visible_t{true}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::LOAD_GENERATED};
}

constexpr table_meta_data table_meta_data::make_for_load_generated_auto_merge_table() noexcept {
  return table_meta_data{user_visible_t{true}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::LOAD_GENERATED_AUTO_MERGE};
}

constexpr table_meta_data table_meta_data::make_for_load_generated_object_link_intermediate_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE};
}

constexpr table_meta_data table_meta_data::make_for_load_generated_object_link_edge_table() noexcept {
  return table_meta_data{user_visible_t{false}, generated_t{true}, system_table_t{false},
                         table_meta_data::table_type::LOAD_GENERATED_SIGNAL_LINK_EDGE};
}

constexpr table_meta_data table_meta_data::make_for_augmentation_table() noexcept {
  return table_meta_data{user_visible_t{true}, generated_t{false}, system_table_t{false},
                         table_meta_data::table_type::AUGMENTATION_TABLE};
}

constexpr table_meta_data table_meta_data::make_for_system_catalog_join_table() noexcept {
  return table_meta_data{user_visible_t{true}, generated_t{true}, system_table_t{true},
                         table_meta_data::table_type::SYSTEM_CATALOG_TABLE, table_system_catalog_type::JOIN_TABLE};
}

}  // namespace memory
}  // namespace celonis::accelerator
