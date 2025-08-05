#pragma once

#include "legacy_embedded_ctl/memory/memory_resource_with_upstream_resource.h"
#include "legacy_embedded_ctl/utils/allocation_priority.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * A memory resource that will check whether an allocation can be performed based on estimated free memory. Will throw
 * if not enough memory.
 */
class checked_memory_resource final : public memory_resource_with_upstream_resource {
 public:
  checked_memory_resource(const utils::allocation_reason& reason, utils::allocation_priority priority,
                          size_t min_bytes_for_check, bool using_value_init = false,
                          abstract_resource_t upstream_resource = nullptr) noexcept;

 private:
  [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

  [[nodiscard]] bool do_is_equal(const abstract_resource_base& other) const noexcept override;

  std::string allocation_reason_;
  legacy_embedded_ctl::source_location source_location_;
  utils::allocation_priority priority_;
  size_t min_bytes_for_check_;
  bool using_value_init_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl