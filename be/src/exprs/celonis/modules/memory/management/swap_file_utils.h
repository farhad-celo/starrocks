#pragma once

#include <string>

#include "format/json/json_fwd.h"

namespace celonis::accelerator::memory::management {
format::json::json_object_t to_json_swap_file_info(const std::string& swap_file, const std::string& description,
                                                   size_t size_in_memory, size_t size_on_disk);
}  // namespace celonis::accelerator::memory::management