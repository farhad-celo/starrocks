#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/management/data_handler_fwd.h"
#include "modules/memory/management/memory_manager_fwd.h"

namespace celonis::accelerator::memory::management {

struct memory_entity_info {
  std::string description{};
  size_t size_on_disk{0};
  size_t size_in_memory{0};
  size_t access_count{0};
  std::string last_access{};
  std::string state{};
};

struct memory_group_info {
  std::string description{};
  std::string table_id{};
  std::vector<memory_entity_info> entity_infos{};
};

// Groups multiple data handlers together. For example a column consists of multiple data handles. In case of a
// dictified column this are the dictionary and the column pointers.
class managed_memory_group {
 public:
  explicit managed_memory_group(std::string type);
  explicit managed_memory_group(std::string type, std::string table_id);

  void add_to_group(const data_handler_t& handler);

  void clear_group();

  size_t get_size_on_disk() const;

  size_t get_size_in_memory() const;

  void force_swap_in(const common::execution_context& context) const;

#ifndef CELOSTAR
  void force_swap_out(common::execution_context& context);

  void force_compress() const;
#endif

  [[nodiscard]] bool is_persisted() const;

#ifndef CELOSTAR
  void swap_out(const std::thread::id& transaction_id,
                const std::chrono::steady_clock::time_point& transaction_start_timestamp,
                common::execution_context& context);
#endif

  memory_group_info dump_header() const;

  const std::string& get_type() const;

  template <class FUNCTION>
  void apply(const FUNCTION& to_apply) const {
    std::shared_lock lck(handlers_mutex);
    std::for_each(handlers.begin(), handlers.end(), [&to_apply, this](auto& weak_ptr) {
      auto shared_ptr = weak_ptr.lock();
      if (shared_ptr != nullptr) {
        to_apply(shared_ptr);
      } else {
        report_expired();
      }
    });
  }

  template <class FUNCTION>
  void apply_and_remove_dangling_weak(const FUNCTION& to_apply) {
    std::unique_lock lck(handlers_mutex);
    for (auto it = handlers.begin(); it != handlers.end();) {
      auto shared_ptr = it->lock();
      if (shared_ptr != nullptr) {
        to_apply(shared_ptr);
        ++it;
      } else {
        it = remove_and_report_expired(it);
      }
    }
  }

 private:
  using handlers_t = std::set<std::weak_ptr<data_handler>, std::owner_less<std::weak_ptr<data_handler>>>;

  void report_expired() const;
  handlers_t::iterator remove_and_report_expired(handlers_t::iterator handler_it);

  const std::string type;
  const std::string table_id;
  handlers_t handlers;
  // the sole purpose of this mutex is to protect the std::set handlers
  mutable std::shared_mutex handlers_mutex;
};

// A volatile group allows to add clean up callback. This one is called if the memory manager decides to delete the
// data. The callback can for example delete the cache entry.
class volatile_managed_memory_group : public managed_memory_group {
 public:
  void erase() {
    if (clean_up_callback != nullptr) {
      clean_up_callback();
    }
  }

  volatile_managed_memory_group(std::string type, std::function<bool()> clean_up_callback)
      : managed_memory_group(std::move(type)), clean_up_callback(std::move(clean_up_callback)) {}

  volatile_managed_memory_group(std::string type, std::string table_id, std::function<bool()> clean_up_callback)
      : managed_memory_group(std::move(type), std::move(table_id)), clean_up_callback(std::move(clean_up_callback)) {}

 private:
  std::function<bool()> clean_up_callback;
};

}  // namespace celonis::accelerator::memory::management
