#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/management/const_bitset_data_accessor_fwd.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/null_flags_fwd.h"
#include "modules/memory/types.h"

/**
 * Storage for a bitset. The data can be swapped, compression is currently not supported.
 */
namespace celonis::accelerator::memory::management {

class swappable_bitset : public data_handler {
 public:
  static constexpr const char* BITSET_SWAP_IN_KEY = "BITSET_SWAP_IN";
  static constexpr const char* BITSET_SWAP_OUT_KEY = "BITSET_SWAP_OUT";

  static std::shared_ptr<swappable_bitset> create_data_handler(const memory::null_flags_t& data,
                                                               const std::string& swap_file, const swap_info& sinfo,
                                                               const std::string& description);

  static std::shared_ptr<swappable_bitset> init_from_swap(const std::string& swap_file, const swap_info& sinfo,
                                                          const std::string& description);

  load_status get_load_status() const override { return status; };

  persistence_status get_persistence_status() const override { return swap_information.persistence_state(); };

  bool swap_file_broken() const override { return broken_swap_file; }

#ifndef CELOSTAR
  bool swap_out(common::execution_context& context) override;

  void write_out(common::execution_context& context);
#endif

  using const_data_accessor_t = const_bitset_data_accessor;

  const_data_accessor_t get_const_data(const common::execution_context& context);

  bool is_swappable() const override;

#ifndef CELOSTAR
  bool compress() override;
#endif

  size_t get_size_in_memory() const override;

  size_t get_size() const { return size; };

  size_t get_size_on_disk() const override { return size_on_disk; };

  size_t get_usage_count() const override { return usage_count; };

  [[nodiscard]] usage_time_t get_last_usage() const override { return usage_time_t{last_usage}; };

  std::thread::id get_loaded_by() const override { return loaded_by; }

  [[nodiscard]] load_time_t get_loaded_at() const override { return load_time_t{loaded_at}; }

  std::string description() const override { return desc; };

  void swap_in(const common::execution_context& context) override { load_from_swap(context); }

  bool is_persisted() const override { return persisted; }

  void set_delete_from_disk_when_destructed(bool value);

  ~swappable_bitset() override;

 private:
  swappable_bitset(load_status status, memory::null_flags_t data, size_t size, std::string swap_file,
                   swap_info swap_information, bool persisted, size_t size_on_disk, std::string description);

  memory::null_flags_t load_from_swap(const common::execution_context& context);

  void swap_out_bitset(common::execution_context& context);

  void write_to_disk(common::execution_context& context);

  mutable std::shared_mutex data_mutex;
  std::atomic<load_status> status;
  memory::null_flags_t data;
  std::atomic<size_t> size;
  std::atomic<size_t> size_on_disk;
  std::atomic<mem_time_t> last_usage;
  std::atomic<std::thread::id> loaded_by;
  std::atomic<mem_time_t> loaded_at;
  std::string swap_file;
  swap_info swap_information;
  std::atomic<bool> persisted;
  std::atomic<size_t> usage_count;
  std::string desc;
  std::atomic<bool> broken_swap_file{false};
  std::atomic<bool> delete_from_disk_when_destructed_{false};
};
}  // namespace celonis::accelerator::memory::management
