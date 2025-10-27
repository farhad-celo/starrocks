#pragma once

#include <thread>
#include <type_traits>

#include "legacy_embedded_ctl/mutex.h"
#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/common/execution_context_fwd.h"
#ifndef CELOSTAR
#include "modules/io/compressed_data.h"
#endif
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/raw_data_handler_fwd.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory::management {

enum class pointer_data_handler_swap_type { NO_SWAP, SWAPPED_DICTIONARY, SWAPPED_MATERIALIZED };

template <typename T>
struct loaded_data {
  legacy_embedded_ctl::shared_static_array<T> data;
};

struct swapped_data {};

template <typename T>
#ifdef CELOSTAR
using data_handler_data = std::variant<loaded_data<T>>;
#else
using data_handler_data = std::variant<loaded_data<T>, io::compressed_data, swapped_data>;
#endif

/**
 * Storage for a list of pointers and the data to which the data points to.
 */
template <typename T>
class pointer_data_handler final : public data_handler {
 private:
  using POINTER_T = T;
  using STORAGE_T = std::remove_const_t<std::remove_pointer_t<T>>;

 public:
  [[nodiscard]] static std::shared_ptr<pointer_data_handler<T>> create_data_handler(
      const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr,
      const legacy_embedded_ctl::shared_static_array<STORAGE_T>& buffer, const std::string& swap_file,
      pointer_data_handler_swap_type type, swap_info sinfo, const std::string& description);

  [[nodiscard]] static std::shared_ptr<pointer_data_handler<T>> create_data_handler(
      legacy_embedded_ctl::static_array<POINTER_T>&& ptr, legacy_embedded_ctl::static_array<STORAGE_T>&& buffer,
      const std::string& swap_file, pointer_data_handler_swap_type type, swap_info sinfo,
      const std::string& description);

  [[nodiscard]] static std::shared_ptr<pointer_data_handler> create_temp_data_handler(
      const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr,
      const legacy_embedded_ctl::shared_static_array<STORAGE_T>& str_buffer);

  [[nodiscard]] static std::shared_ptr<pointer_data_handler> create_temp_data_handler(
      legacy_embedded_ctl::static_array<POINTER_T>&& ptr, legacy_embedded_ctl::static_array<STORAGE_T>&& str_buffer);

#ifndef CELOSTAR
  [[nodiscard]] static std::shared_ptr<pointer_data_handler<T>> init_from_swap(const std::string& swap_file,
                                                                               swap_info sinfo,
                                                                               pointer_data_handler_swap_type type,
                                                                               const std::string& description);
#endif

  [[nodiscard]] load_status get_load_status() const override;

  [[nodiscard]] load_time_t get_loaded_at() const override;

  [[nodiscard]] persistence_status get_persistence_status() const override;

  [[nodiscard]] bool is_swappable() const override;

  [[nodiscard]] bool swap_file_broken() const override;

#ifndef CELOSTAR
  bool swap_out(common::execution_context& context) override;

  bool compress() override;
#endif

  using const_data_accessor_t = const_data_accessor<T>;

  const_data_accessor_t get_const_data(const common::execution_context& context);

  [[nodiscard]] size_t get_pointer_buffer_size() const;

  [[nodiscard]] size_t get_size() const;

  [[nodiscard]] size_t get_size_in_memory() const override;

  [[nodiscard]] size_t get_size_on_disk() const override;

  [[nodiscard]] bool is_persisted() const override;

  [[nodiscard]] size_t get_usage_count() const override;

  [[nodiscard]] usage_time_t get_last_usage() const override;

  [[nodiscard]] std::thread::id get_loaded_by() const override;

  [[nodiscard]] std::string description() const override;

  ~pointer_data_handler() override;

#ifndef CELOSTAR
  bool write_out(common::execution_context& context);
#endif

  void swap_in(const common::execution_context& context) override;

  void set_delete_from_disk_when_destructed(bool value);

 private:
  // a wrapper so that the type can be put into the owning_mutex
  struct data_wrapper {
    data_handler_data<POINTER_T> pointer;
    data_handler_data<STORAGE_T> buffer;
  };

  pointer_data_handler(const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr_data,
                       const legacy_embedded_ctl::shared_static_array<STORAGE_T>& buffer_data, swap_info sinfo,
                       const std::string& base_swap_file, pointer_data_handler_swap_type type, std::string description);

#ifndef CELOSTAR
  pointer_data_handler(size_t pointer_size, size_t buffer_size, size_t pointer_size_on_disk, size_t buffer_size_on_disk,
                       swap_info sinfo, const std::string& base_swap_file, pointer_data_handler_swap_type type,
                       std::string description);

  void register_files();

  /**
   * @brief only writes out pointers if they are not in usage and returns true, other returns false
   */
  [[nodiscard]] bool write_out_if_applicable(const data_wrapper& data, common::execution_context& context);

  void write_out_pointers(const loaded_data<POINTER_T>& pointers, const loaded_data<STORAGE_T>& buffer,
                          common::execution_context& context);

  void write_out_pointers(const io::compressed_data& compressed_pointers, common::execution_context& context);

  void write_out_buffer(const data_wrapper& data, common::execution_context& context);

  void write_out_buffer(const loaded_data<STORAGE_T>& buffer, const loaded_data<POINTER_T>& pointers,
                        common::execution_context& context);

  void write_out_buffer(const io::compressed_data& compressed_buffers, common::execution_context& context);
#endif

  void swap_in_data(data_wrapper& data, common::timer& timer_with_lock, common::timer& timer_after_lock,
                    const common::execution_context& context);

  void swap_in_pointers(data_handler_data<POINTER_T>& pointer, const loaded_data<STORAGE_T>& buffer,
                        const common::execution_context& context);

  void swap_in_buffer(data_handler_data<STORAGE_T>& buffer, const common::execution_context& context);

#ifndef CELOSTAR
  template <typename TYPE>
  [[nodiscard]] loaded_data<TYPE> swap_in_impl(const std::string& swap_file, std::atomic<size_t>& size,
                                               std::atomic<size_t>& size_on_disk, std::atomic<bool>& broken_swap_file)
    requires(std::is_same_v<TYPE, STORAGE_T> || std::is_same_v<TYPE, POINTER_T>);
#endif

  // The following functions are used only for enabling gdb_verify_command_test.py - CPL-9353
  [[nodiscard]] const STORAGE_T* get_buffer_start() const;
  [[nodiscard]] size_t get_buffer_size() const;
  [[nodiscard]] const POINTER_T* get_pointer_start() const;
  [[nodiscard]] size_t get_pointer_size() const;

  std::atomic<size_t> pointer_size_on_disk_{0};
  std::atomic<size_t> buffer_size_on_disk_{0};

  std::string pointer_swap_file_;
  std::string buffer_swap_file_;

  std::atomic<bool> pointer_persisted_{false};
  std::atomic<bool> buffer_persisted_{false};

  std::atomic<bool> pointer_broken_swap_file_{false};
  std::atomic<bool> buffer_broken_swap_file_{false};

  std::atomic<size_t> pointer_size_{0};
  std::atomic<size_t> buffer_size_{0};

  legacy_embedded_ctl::owning_mutex<data_wrapper> data_;

  std::atomic<size_t> usage_count_{0};
  std::atomic<mem_time_t> last_usage_{};
  std::atomic<bool> delete_from_disk_when_destructed_{false};

  std::atomic<std::thread::id> loaded_by_{std::this_thread::get_id()};
  std::atomic<mem_time_t> loaded_at_{};

  swap_info sinfo_;
  std::string desc_;
  bool write_with_sorting_;
};

using string_data_handler = pointer_data_handler<cel_string_t>;

}  // namespace celonis::accelerator::memory::management
