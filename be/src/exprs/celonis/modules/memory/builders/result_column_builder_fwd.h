#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "modules/memory/column_fwd.h"

namespace celonis::accelerator::memory::builders {
class result_column_builder;
using result_column_builder_t = std::shared_ptr<result_column_builder>;
using cache_result_column_builder_map_t = std::unordered_map<std::string, result_column_builder_t>;
using build_result_columns_t = std::function<std::unordered_map<std::string, memory::column_t>(
    const memory::builders::cache_result_column_builder_map_t&)>;
}  // namespace celonis::accelerator::memory::builders
