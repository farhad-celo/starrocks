#pragma once

#include <ctl/bitset.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/management/const_bitset_data_accessor_fwd.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/raw_data_handler_fwd.h"
#include "modules/memory/null_flags_fwd.h"

/**
 * @brief Storage for a bitset. The data can be swapped, compression is currently not supported.
 * Internally it contains a raw_data_handler<uint64_t>. During swap out, the dense array
 * is written to a bitset ending swap file ("bs"). Since reading of the dense array swap file
 * is not supported by other old release versions in our deployment, we also need to write out
 * the old null flag ending swap file ("nf"/"np") which contains a boolean array. This will
 * be removed once CPL-9839 is deployed everywhere in the future.
 */
namespace celonis::accelerator::memory::management {

class swappable_bitset : public data_handler {
 public:
  static std::shared_ptr<swappable_bitset> create_data_handler(const memory::null_flags_t& data,
                                                               const std::string& swap_file, const swap_info& sinfo,
                                                               const std::string& description);

  [[nodiscard]] load_status get_load_status() const override;

  [[nodiscard]] persistence_status get_persistence_status() const override;

  [[nodiscard]] bool swap_file_broken() const override;

  using const_data_accessor_t = const_bitset_data_accessor;

  const_data_accessor_t get_const_data(const common::execution_context& context);

  [[nodiscard]] bool is_swappable() const override;

  [[nodiscard]] size_t get_size_in_memory() const override;

  [[nodiscard]] size_t get_size() const;

  [[nodiscard]] size_t get_size_on_disk() const override;

  [[nodiscard]] size_t get_usage_count() const override;

  [[nodiscard]] usage_time_t get_last_usage() const override;

  [[nodiscard]] std::thread::id get_loaded_by() const override;

  [[nodiscard]] load_time_t get_loaded_at() const override;

  [[nodiscard]] std::string description() const override;

  void swap_in(const common::execution_context& context) override;

  [[nodiscard]] bool is_persisted() const override;

  void set_delete_from_disk_when_destructed(bool value);

  ~swappable_bitset() override;

 private:
  swappable_bitset(memory::management::raw_data_handler_t<uint64_t>&& data_handler, std::string swap_file,
                   swap_info sinfo, std::optional<size_t> size = {});

  void write_null_flag_ending_to_disk(common::execution_context& context);

  size_t size_{0};
  memory::management::raw_data_handler_t<uint64_t> data_handler_;
  // below are for backwards compatibility
  mutable std::shared_mutex data_mutex_;
  size_t size_on_disk_{0};
  std::string swap_file_;  // null flag ending swap file
  swap_info sinfo_;
  std::atomic<bool> persisted_{false};
  std::atomic<bool> delete_from_disk_when_destructed_{false};
};
}  // namespace celonis::accelerator::memory::management
