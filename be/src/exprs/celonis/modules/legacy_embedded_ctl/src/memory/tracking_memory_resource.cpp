#include "legacy_embedded_ctl/memory/tracking_memory_resource.h"

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {

void register_if_not_null(memory_tracking_strategy* tracking_strategy, const std::size_t bytes) {
  legacy_embedded_debug_assert(tracking_strategy != nullptr);
  if (tracking_strategy != nullptr) {
    tracking_strategy->register_allocation(bytes);
  }
}

void deregister_if_not_null(memory_tracking_strategy* tracking_strategy, const std::size_t bytes) {
  legacy_embedded_debug_assert(tracking_strategy != nullptr);
  if (tracking_strategy != nullptr) {
    tracking_strategy->deregister_allocation(bytes);
  }
}

}  // namespace

void* tracking_memory_resource::do_allocate(const std::size_t bytes, const std::size_t alignment) {
  void* alloc{get_upstream_memory_resource()->allocate(bytes, alignment)};
  register_if_not_null(tracking_strategy_.get(), bytes);
  return alloc;
}

void tracking_memory_resource::do_deallocate(void* p, const std::size_t bytes, const std::size_t alignment) {
  get_upstream_memory_resource()->deallocate(p, bytes, alignment);
  deregister_if_not_null(tracking_strategy_.get(), bytes);
}

bool tracking_memory_resource::do_is_equal(const abstract_resource_base& other) const noexcept {
  const auto* const other_tracking_resource{dynamic_cast<const tracking_memory_resource*>(std::addressof(other))};
  if (other_tracking_resource == nullptr) {
    return false;
  }
  if (tracking_strategy_ == nullptr && other_tracking_resource->tracking_strategy_ != nullptr) {
    return false;
  }
  bool strategies_equal{(tracking_strategy_ == nullptr && other_tracking_resource->tracking_strategy_ == nullptr) ||
                        tracking_strategy_->is_equal(*other_tracking_resource->tracking_strategy_)};
  bool upstream_resources_equal{
      get_upstream_memory_resource()->is_equal(*other_tracking_resource->get_upstream_memory_resource())};
  return strategies_equal && upstream_resources_equal;
}

abstract_resource_t tracking_memory_resource::get_global_tracking_resource() noexcept {
  // wrap default strategy in shared ptr without control block, so it points to static stack object instead of heap
  static tracking_memory_resource global_resource{memory_tracking_strategy::get_default_strategy()};
  static abstract_resource_t global_resource_ptr{
      std::shared_ptr<abstract_resource_base>{std::shared_ptr<void>{}, &global_resource}};
  return global_resource_ptr;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
