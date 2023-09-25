#include "table_meta_data.h"

#ifndef CELOSTAR
#include "modules/common/exceptions.h"
#include "modules/query/queries.pb.h"

namespace celonis::accelerator::memory {

namespace {

[[nodiscard]] TableMetaData_TableType table_type_to_proto(const table_meta_data::table_type table_type) {
  switch (table_type) {
    case table_meta_data::INVALID:
      return TableMetaData::INVALID_TABLE_TYPE;
    case table_meta_data::DATA_MODEL_TABLE:
      return TableMetaData::DATA_MODEL_TABLE;
    case table_meta_data::USER_AUTHENTICATION_TABLE:
      return TableMetaData::USER_AUTHENTICATION_TABLE;
    case table_meta_data::QUERY_SCOPE_TABLE:
      return TableMetaData::QUERY_SCOPE_TABLE;
    case table_meta_data::QUERY_SCOPE_AGGREGATION_TABLE:
      return TableMetaData::QUERY_SCOPE_AGGREGATION_TABLE;
    case table_meta_data::QUERY_SCOPE_RESULT_TABLE:
      return TableMetaData::QUERY_SCOPE_RESULT_TABLE;
    case table_meta_data::OPERATOR_TABLE:
      return TableMetaData::OPERATOR_TABLE;
    case table_meta_data::LOAD_GENERATED:
      return TableMetaData::LOAD_GENERATED;
    case table_meta_data::LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE:
      return TableMetaData::LOAD_GENERATED_SIGNAL_LINK_INTERMEDIATE;
    case table_meta_data::LOAD_GENERATED_AUTO_MERGE:
      return TableMetaData::LOAD_GENERATED_AUTO_MERGE;
    case table_meta_data::AUGMENTATION_TABLE:
      return TableMetaData::AUGMENTATION_TABLE;
    case table_meta_data::SYSTEM_CATALOG_TABLE:
      return TableMetaData::SYSTEM_CATALOG_TABLE;
    case table_meta_data::LOAD_GENERATED_SIGNAL_LINK_EDGE:
      return TableMetaData::LOAD_GENERATED_SIGNAL_LINK_EDGE;
  }
  throw common::internal_exception{"Unknown table type in internal_table_type_to_proto()."};
}

}  // anonymous namespace

void table_meta_data_to_proto(TableMetaData* const table_meta_data_proto, const table_meta_data& meta_data) {
  table_meta_data_proto->set_is_visible(meta_data.is_user_visible());
  table_meta_data_proto->set_is_generated(meta_data.is_generated());
  table_meta_data_proto->set_table_type(table_type_to_proto(meta_data.get_table_type()));
}

}  // namespace celonis::accelerator::memory
#endif