#include "managed_memory_group.h"

#include <mutex>

#include <fmt/chrono.h>

#include "log/log.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory::management {

void managed_memory_group::report_expired() const { log::info("skipping expired weak ptr for group {}", type); }

// This function is already protected with a lock by the caller function
managed_memory_group::handlers_t::iterator managed_memory_group::remove_and_report_expired(
    const handlers_t::iterator handler_it) {
  log::jwarn("Removing expired weak ptr for memory group.", {{"group_description", type}});
  return handlers.erase(handler_it);
}

managed_memory_group::managed_memory_group(std::string type) : type(std::move(type)), table_id(std::string{}) {}

managed_memory_group::managed_memory_group(std::string type, std::string table_id)
    : type(std::move(type)), table_id(std::move(table_id)) {}

void managed_memory_group::add_to_group(const std::shared_ptr<data_handler>& handler) {
  std::lock_guard lck(handlers_mutex);
  handlers.insert(handler);
}

void managed_memory_group::clear_group() {
  std::lock_guard lck(handlers_mutex);
  handlers.clear();
}

size_t managed_memory_group::get_size_in_memory() const {
  size_t sum{0};
  apply([&sum](const std::shared_ptr<data_handler>& dh) { sum += dh->get_size_in_memory(); });
  return sum;
}

const std::string& managed_memory_group::get_type() const { return type; }

}  // namespace celonis::accelerator::memory::management
