#pragma once

#include <string>

#include "modules/common/shared_types.h"

namespace celonis::accelerator::memory {

struct column_info final {
  std::string name{};
  std::string id{};
  std::string cache_key{};
  std::string domain_column{};
  std::string format{};
  data_type type{};
};

}  // namespace celonis::accelerator::memory
