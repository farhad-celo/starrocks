#include "ctl/source_location.h"

#include "ctl/hash.h"

namespace celonis::accelerator::ctl {

std::string source_location::hash_file_name() const {
  if constexpr (ctl::IS_DEBUG_BUILD) {
    return file_name_without_path();
  }
  return fmt::format("{:#018x}", hash_fnv1a64(file_name_without_path()));
}

}  // namespace celonis::accelerator::ctl
