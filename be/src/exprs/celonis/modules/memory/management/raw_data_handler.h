#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>

#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/common/execution_context.h"
#ifndef CELOSTAR
#include "modules/io/compressed_data.h"
#endif
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/load_status.h"
#include "modules/memory/management/raw_data_handler_fwd.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/types.h"

/**
 * Storage for an array of data. The data can be compressed or swapped out.
 */
namespace celonis::accelerator::memory::management {

template <typename T>
class raw_data_handler final : public data_handler {
 public:
  static constexpr const char* RAW_DATA_HANDLER_SWAP_IN_KEY = "RAW_DATA_HANDLER_SWAP_IN";
  static constexpr const char* RAW_DATA_HANDLER_SWAP_OUT_KEY = "RAW_DATA_HANDLER_SWAP_OUT";

  static raw_data_handler_t<T> create_data_handler(legacy_embedded_ctl::static_array<T> data, const std::string& swap_file,
                                                   const swap_info& sinfo, const std::string& description);

  static raw_data_handler_t<T> create_temp_data_handler(legacy_embedded_ctl::static_array<T> data);

  static raw_data_handler_t<T> create_data_handler(const legacy_embedded_ctl::shared_static_array<T>& data,
                                                   const std::string& swap_file, const swap_info& sinfo,
                                                   const std::string& description);

  [[nodiscard]] static std::shared_ptr<raw_data_handler<T>> create_temp_data_handler(
      const legacy_embedded_ctl::shared_static_array<T>& data);

#ifndef CELOSTAR
  /**
   * Constructs a raw data handler whose data already exists in the specified swap file on disk.
   * For this operation only the data size stored in the swap file header is read, but not the actual data.
   * The underlying data remains swapped out after initialization of the raw data handler.
   *
   * @return raw data handler with load_status::SWAPPED which is associated to the given swap file. A nullptr is
   * returned if the swap file does not exist.
   */
  static raw_data_handler_t<T> init_from_swap(const std::string& swap_file, const swap_info& sinfo,
                                              const std::string& description);
#endif

  load_status get_load_status() const override { return status; };

  persistence_status get_persistence_status() const override { return swap_information.persistence_state(); };

  bool swap_file_broken() const override { return broken_swap_file; }

  bool is_swappable() const override { return swap_information.is_swappable(); }

#ifndef CELOSTAR
  bool compress() override;
#endif

  size_t get_size_in_memory() const override;

  size_t get_size() const { return size; };

  size_t get_size_on_disk() const override { return size_on_disk; };

  size_t get_usage_count() const override { return usage_count; };

  bool is_persisted() const override { return persisted; };

  [[nodiscard]] usage_time_t get_last_usage() const override { return usage_time_t{last_usage}; }

  std::thread::id get_loaded_by() const override { return loaded_by; }

  [[nodiscard]] load_time_t get_loaded_at() const override { return load_time_t{loaded_at}; }

#ifndef CELOSTAR
  bool swap_out(common::execution_context& context) override;

  void write_out(const common::execution_context& context);
#endif

  using const_data_accessor_t = const_data_accessor<T>;

  /**
   * This function returns a pointer to the immutable stored data.
   */
  const_data_accessor_t get_const_data(const common::execution_context& context = {}) requires(
      requires(legacy_embedded_ctl::shared_static_array<T> ptr) { const_data_accessor<T>{ptr}; });

  std::string description() const override { return desc; }

  // swap the column into memory
  void swap_in(const common::execution_context& context) override { swap_in_data(context); }

  void set_delete_from_disk_when_destructed(const bool value) { delete_from_disk_when_destructed_.store(value); }

  ~raw_data_handler() override;

 private:
  raw_data_handler(load_status status, legacy_embedded_ctl::shared_static_array<T> data, size_t size, std::string swap_file,
                   swap_info swap_information, bool persisted, size_t size_on_disk, std::string description);

  legacy_embedded_ctl::shared_static_array<T> swap_in_data(const common::execution_context& context);

#ifndef CELOSTAR
  void swap_to_disk(common::execution_context& context);

  void write_to_disk(const common::execution_context& context);

  [[nodiscard]] const io::compressed_data& compressed_data() const;

  [[nodiscard]] size_t compressed_data_size() const;

  /**
   * After locking, all validity checks and compression succeeded in 'compress', set the data and post conditions
   */
  void init_compressed_state(io::compressed_data&& compressed_data);
#endif

  mutable std::shared_mutex data_mutex;
  std::atomic<load_status> status;
  legacy_embedded_ctl::shared_static_array<T> data;
  std::atomic<size_t> size;
  std::atomic<size_t> size_on_disk;
#ifndef CELOSTAR
  // TODO(n.weber): Maybe a strong type for the different states the data can be in makes sense (ensuring all post
  // conditions such as 'status' are always correctly set
  std::optional<io::compressed_data> compressed_data_{std::nullopt};
#endif
  std::atomic<mem_time_t> last_usage{mem_clock_t::now()};
  std::atomic<std::thread::id> loaded_by{std::thread::id{}};
  std::atomic<mem_time_t> loaded_at{mem_clock_t::time_point{}};
  std::string swap_file;
  swap_info swap_information;
  std::atomic<bool> persisted;
  std::atomic<size_t> usage_count{0};
  std::string desc;
  std::atomic<bool> broken_swap_file{false};
  std::atomic<bool> delete_from_disk_when_destructed_{false};
};
}  // namespace celonis::accelerator::memory::management
