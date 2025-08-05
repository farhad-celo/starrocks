#pragma once

#include <memory>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>

#include "legacy_embedded_ctl/checked_ptr.h"

namespace celonis::accelerator::legacy_embedded_ctl {

using abstract_resource_base = boost::container::pmr::memory_resource;
using abstract_resource_t = legacy_embedded_ctl::checked_shared_ptr<abstract_resource_base>;

[[nodiscard]] inline abstract_resource_t get_default_memory_resource() {
  return std::shared_ptr<abstract_resource_base>{std::shared_ptr<void>{},
                                                 boost::container::pmr::get_default_resource()};
}

/**
 * An (abstract) memory resource that has an pointer to an upstream resource. It has a getter for the owned upstream
 * resource that will return the default memory resource the pointer is a nullptr.
 */
class memory_resource_with_upstream_resource : public abstract_resource_base {
 public:
  explicit memory_resource_with_upstream_resource(abstract_resource_t upstream_resource = nullptr) noexcept
      : upstream_resource_{upstream_resource == nullptr ? get_default_memory_resource()
                                                        : std::move(upstream_resource)} {}

  ~memory_resource_with_upstream_resource() noexcept override = default;

  memory_resource_with_upstream_resource(const memory_resource_with_upstream_resource&) = delete;
  memory_resource_with_upstream_resource(memory_resource_with_upstream_resource&&) = delete;
  memory_resource_with_upstream_resource& operator=(const memory_resource_with_upstream_resource&) = delete;
  memory_resource_with_upstream_resource& operator=(memory_resource_with_upstream_resource&&) = delete;

 protected:
  [[nodiscard]] abstract_resource_base* get_upstream_memory_resource() const { return upstream_resource_.get(); }

 private:
  abstract_resource_t upstream_resource_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
