#include "format/json/json.h"

namespace celonis::accelerator::memory::management {
format::json::json_object_t to_json_swap_file_info(const std::string& swap_file, const std::string& description,
                                                   const size_t size_in_memory, const size_t size_on_disk) {
  format::json::json_object_t output;
  output["swap_file.name"] = swap_file;
  output["swap_file.description"] = description;
  output["swap_file.size.memory"] = size_in_memory;
  output["swap_file.size.disk"] = size_on_disk;
  return output;
}
}  // namespace celonis::accelerator::memory::management