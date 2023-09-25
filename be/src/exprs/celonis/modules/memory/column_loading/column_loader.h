#pragma once

#include <memory>

#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/materialized_data.h"
#include "modules/memory/typed_dictionary.h"

namespace celonis::accelerator::memory::column_loading {

struct column_config {
  data_type type{};
  std::string name;
  std::string id;
  std::string cache_key;
  management::swap_info swap_information{management::no_swap()};
  row_id row_count{0};
  std::string description;
};

inline std::ostream& operator<<(std::ostream& os, const column_config& column_config) {
  auto name = column_config.name;
  std::replace(name.begin(), name.end(), '"', '\0');
  return os << R"("column_config": {"data_type":)" << column_config.type << R"(, "name":")" << name
            << R"(", "row_count":)" << column_config.row_count << "}";
}

enum column_status {
  MISSING,       // Column is not loaded yet.
  MATERIALIZED,  // Means the data is stored simply in an array.
  DICTIFIED      // Dictionary compressed. Most of the time this needs less memory, but the dictionary is expensive to
                 // create
};

struct dictified_column_data {
  column_ptrs_t col_ptrs;
  std::shared_ptr<dictionary> dict;
};

using column_data_t = std::variant<dictified_column_data, std::shared_ptr<materialized_data>>;

class column_loader {
 public:
  /**
   * loads the data of the column specified in the config from the data source and stores the data in the column.
   * It either sets plain_data (if loaded data is not dictified yet) or column_pointers and dict (if dictified data is
   * loaded).
   * @param col
   * @param config
   */
  virtual column_data_t load_missing_column(const column_config& col, const common::execution_context& context) = 0;

  virtual ~column_loader() = default;
};

}  // namespace celonis::accelerator::memory::column_loading
