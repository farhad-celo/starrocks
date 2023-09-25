#pragma once

#include <memory>

namespace celonis::accelerator::cube::execution::tracking {

class operator_statistics;
class operator_statistics_per_query;
using operator_statistics_t = std::shared_ptr<operator_statistics>;
using operator_statistics_per_query_t = std::shared_ptr<operator_statistics_per_query>;

}  // namespace celonis::accelerator::cube::execution::tracking
