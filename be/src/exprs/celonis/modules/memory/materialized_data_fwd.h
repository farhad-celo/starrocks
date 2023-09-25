#pragma once

#include <memory>

namespace celonis::accelerator::memory {
class materialized_data;
using materialized_data_t = std::shared_ptr<materialized_data>;
template <typename T>
class materialized_typed_data;
template <typename T>
using materialized_typed_data_t = std::shared_ptr<materialized_typed_data<T>>;
}  // namespace celonis::accelerator::memory
