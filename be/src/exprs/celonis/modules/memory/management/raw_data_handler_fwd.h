#pragma once
#include <memory>

namespace celonis::accelerator::memory::management {
template <typename T>
class raw_data_handler;

template <typename T>
using raw_data_handler_t = std::shared_ptr<raw_data_handler<T>>;
}  // namespace celonis::accelerator::memory::management
