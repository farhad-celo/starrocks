#include "managed_memory_group.h"

#include <mutex>

#include <fmt/chrono.h>

#include "log/log.h"
#include "modules/memory/table.h"

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

size_t managed_memory_group::get_size_on_disk() const {
  size_t sum{0};
  apply([&sum](const std::shared_ptr<data_handler>& dh) { sum += dh->get_size_on_disk(); });
  return sum;
}

size_t managed_memory_group::get_size_in_memory() const {
  size_t sum{0};
  apply([&sum](const std::shared_ptr<data_handler>& dh) { sum += dh->get_size_in_memory(); });
  return sum;
}

bool managed_memory_group::is_persisted() const {
  bool persisted = true;
  apply([&persisted](const std::shared_ptr<data_handler>& dh) { persisted &= dh->is_persisted(); });
  return persisted;
}

void managed_memory_group::force_swap_in(const common::execution_context& context) const {
  apply([&context](const std::shared_ptr<data_handler>& dh) { dh->swap_in(context); });
}

#ifndef CELOSTAR
void managed_memory_group::force_swap_out(common::execution_context& context) {
  apply_and_remove_dangling_weak([&context](const std::shared_ptr<data_handler>& dh) { dh->swap_out(context); });
}

void managed_memory_group::force_compress() const {
  apply([](const std::shared_ptr<data_handler>& dh) { dh->compress(); });
}
#endif

namespace {
[[nodiscard]] std::string to_iso_extended_string(const mem_time_t time) {
  auto converted_time{std::chrono::system_clock::now() +
                      duration_cast<std::chrono::system_clock::duration>(time - mem_clock_t::now())};

#ifdef CELOSTAR
  return fmt::format("{:%FT%TZ}", converted_time);
#else
  // TODO(j.boettcher) When clang supports it, we could replace this conversion using
  // std::format("{:%FT%TZ}", converted_time)
  return ::date::format("{:%FT%TZ}", converted_time);
#endif
}
}  // namespace

memory_group_info managed_memory_group::dump_header() const {
  memory_group_info group_info;
  group_info.description = type;
  group_info.table_id = table_id;
  apply([&group_info](const std::shared_ptr<data_handler>& dh) {
    memory_entity_info entity_info;
    entity_info.description = dh->description();
    entity_info.size_on_disk = dh->get_size_on_disk();
    entity_info.size_in_memory = dh->get_size_in_memory();
    entity_info.access_count = dh->get_usage_count();
    entity_info.last_access = to_iso_extended_string(dh->get_last_usage());
    switch (dh->get_load_status()) {
      case load_status::COMPRESSED:
        entity_info.state = "compressed";
        break;
      case load_status::LOADED:
        entity_info.state = "loaded";
        break;
      case load_status::SWAPPED:
        entity_info.state = "swapped";
        break;
    }
    group_info.entity_infos.push_back(entity_info);
  });
  return group_info;
}

const std::string& managed_memory_group::get_type() const { return type; }

#ifndef CELOSTAR
void managed_memory_group::swap_out(const std::thread::id& transaction_id,
                                    const std::chrono::steady_clock::time_point& transaction_start_timestamp,
                                    common::execution_context& context) {
  apply_and_remove_dangling_weak(
      [&transaction_id, &transaction_start_timestamp, &context](const std::shared_ptr<data_handler>& data_handler) {
        if (data_handler->get_loaded_by() == transaction_id &&
            transaction_start_timestamp <= data_handler->get_loaded_at().get()) {
          data_handler->swap_out(context);
        }
      });
}
#endif
}  // namespace celonis::accelerator::memory::management
