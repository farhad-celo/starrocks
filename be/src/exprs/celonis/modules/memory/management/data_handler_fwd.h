#pragma once

#include <memory>

namespace celonis::accelerator::memory::management {

class data_handler;
class managed_memory_group;
class volatile_managed_memory_group;

using volatile_group_t = std::shared_ptr<volatile_managed_memory_group>;
using managed_group_t = std::shared_ptr<managed_memory_group>;
using data_handler_t = std::shared_ptr<data_handler>;

}  // namespace celonis::accelerator::memory::management
