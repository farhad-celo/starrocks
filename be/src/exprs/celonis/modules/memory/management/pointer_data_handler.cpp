#include "pointer_data_handler.h"

#include <memory>
#include <string_view>
#include <type_traits>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utility.h"
#include "log/log.h"
#ifndef CELOSTAR
#include "modules/common/call_and_log_unsafe_callable.h"
#endif
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types.h"
#include "modules/common/timer.h"
#include "modules/common/trace_types.h"
#ifndef CELOSTAR
#include "modules/io/byte_view_utils.h"
#include "modules/io/compress_encrypt_utils.h"
#include "modules/io/file_utils.h"
#include "modules/io/pointer_byte_iterator.h"
#include "modules/io/storage_manager.h"
#include "modules/memory/management/swap_context.h"
#include "modules/memory/management/swap_file_utils.h"
#endif

namespace celonis::accelerator::memory::management {

namespace {

std::string_view get_dict_file_ending(pointer_data_handler_swap_type type) {
  switch (type) {
    case pointer_data_handler_swap_type::SWAPPED_DICTIONARY:
      return DICT_ENDING;
    case pointer_data_handler_swap_type::SWAPPED_MATERIALIZED:
      return MATERIALIZED_DATA_ENDING;
    default:
      return "";
  }
}

std::string_view get_pointer_buffer_file_ending(pointer_data_handler_swap_type type) {
  switch (type) {
    case pointer_data_handler_swap_type::SWAPPED_DICTIONARY:
      return DICT_STR_BUFFER_ENDING;
    case pointer_data_handler_swap_type::SWAPPED_MATERIALIZED:
      return STRING_BUFFER_ENDING;
    default:
      return "";
  }
}

#ifndef CELOSTAR
// Has to be called before one of the underlying data handlers is triggered to write data to disk.
template <typename T>
inline void convert_to_relative_addresses(
    legacy_embedded_ctl::shared_static_array<T>& pointers,
    const legacy_embedded_ctl::shared_static_array<std::remove_const_t<std::remove_pointer_t<T>>>& buffer) noexcept
    requires(std::is_pointer_v<T>) {
  const uintptr_t buffer_offset = reinterpret_cast<uintptr_t>(buffer.get());
  for (size_t i = 0; i < pointers.size(); i++) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    pointers[i] = reinterpret_cast<T>(reinterpret_cast<uintptr_t>(pointers[i]) - buffer_offset);
  }
}

// Has to be called before one of the underlying data handlers is accessed.
template <typename T>
inline void convert_to_absolute_addresses(
    legacy_embedded_ctl::shared_static_array<T>& pointers,
    const legacy_embedded_ctl::shared_static_array<std::remove_const_t<std::remove_pointer_t<T>>>& buffer) noexcept
    requires(std::is_pointer_v<T>) {
  const uintptr_t buffer_offset = reinterpret_cast<uintptr_t>(buffer.get());
  for (size_t i = 0; i < pointers.size(); i++) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    pointers[i] = reinterpret_cast<T>(reinterpret_cast<uintptr_t>(pointers[i]) + buffer_offset);
  }
}

template <typename T>
io::compressed_data compress_pointers(const loaded_data<T>& pointers,
                                      const loaded_data<std::remove_const_t<std::remove_pointer_t<T>>>& buffer,
                                      bool write_with_sorting) requires(std::is_pointer_v<T>) {
  if constexpr (std::is_same_v<T, cel_string_t>) {
    if (write_with_sorting) {
      io::unsorted_string_pointer_byte_iterator pointer_it{std::span{pointers.data},
                                                           reinterpret_cast<uintptr_t>(buffer.data.data())};
      return io::compress_to_memory_stream(pointer_it);
    }
  }

  io::pointer_byte_iterator<T> pointer_it{std::span{pointers.data}, reinterpret_cast<uintptr_t>(buffer.data.data())};
  return io::compress_to_memory_stream(pointer_it);
}

template <typename T>
io::compressed_data compress_buffer(const loaded_data<T>& pointers,
                                    const loaded_data<std::remove_const_t<std::remove_pointer_t<T>>>& buffer,
                                    bool write_with_sorting) requires(std::is_pointer_v<T>) {
  if constexpr (std::is_same_v<T, cel_string_t>) {
    if (write_with_sorting) {
      io::unsorted_string_buffer_byte_iterator iter{std::span{pointers.data}, std::span{buffer.data}};
      return io::compress_to_memory_stream(iter);
    }
  }

  io::byte_iterator iter{io::create_byte_iterator_from(std::span{buffer.data})};
  return io::compress_to_memory_stream(iter);
}

template <typename T>
loaded_data<T> decompress_pointers(
    const io::compressed_data& compressed_pointers, const size_t uncompressed_size,
    const loaded_data<std::remove_const_t<std::remove_pointer_t<T>>>& buffer) requires(std::is_pointer_v<T>) {
  auto pointers{legacy_embedded_ctl::make_shared_static_array_for_overwrite<T>(
      uncompressed_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMORY_FOR_DECOMPRESSION_MSG))};
  io::decompress_from_memory_mt(compressed_pointers, io::as_byte_span(std::span{pointers}));
  convert_to_absolute_addresses(pointers, buffer.data);

  return loaded_data<T>{std::move(pointers)};
}

template <typename T>
loaded_data<T> decompress_buffer(const io::compressed_data& compressed_buffer, const size_t uncompressed_size) {
  auto buffer{legacy_embedded_ctl::make_shared_static_array_for_overwrite<T>(
      uncompressed_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMORY_FOR_DECOMPRESSION_MSG))};
  io::decompress_from_memory_mt(compressed_buffer, io::as_byte_span(std::span{buffer}));

  return loaded_data<T>{std::move(buffer)};
}

template <typename T>
std::optional<size_t> write_out_compressed_data(const io::compressed_data& data, size_t uncompressed_size,
                                                const std::string& swap_file, const swap_info& sinfo,
                                                common::execution_context& context) {
  if (!sinfo.is_swappable()) {
    return std::nullopt;
  }
  return sinfo.storage_manager().encrypt_and_write_mt(swap_file, data, uncompressed_size, sinfo, io::get_type_id<T>(),
                                                      context);
}

void delete_swap_file(const swap_info& sinfo, const std::string& swap_file) {
  const io::storage_manager& sm{sinfo.storage_manager()};
  try {
    sm.delete_swap_file(swap_file, sinfo);
  } catch (const std::exception& e) {
    log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler. Reason: [{}].", swap_file,
              e.what());
  } catch (...) {
    log::warn("Failed to deleted swap file \"{}\" by destructor of raw_data_handler.", swap_file);
  }
}
#endif

}  // anonymous namespace

namespace details {
// The following functions have to be put into the details namespace so that they are not shaded by the
// member functions with the same names

template <typename T>
size_t get_size_in_memory(const data_handler_data<T>& data) {
#ifdef CELOSTAR
  return std::visit(
      legacy_embedded_ctl::overloaded([](const loaded_data<T>& loaded) { return loaded.data.byte_size(); }),
#else
  return std::visit(
      legacy_embedded_ctl::overloaded([](const loaded_data<T>& loaded) { return loaded.data.byte_size(); },
                                      [](const io::compressed_data& compressed) { return compressed.size(); },
                                      [](const swapped_data&) { return size_t{0}; }),
#endif
      data);
}

#ifndef CELOSTAR
template <typename T>
std::optional<size_t> write_out_pointers(const loaded_data<T>& pointers,
                                         const loaded_data<std::remove_const_t<std::remove_pointer_t<T>>>& buffer,
                                         const std::string& swap_file, const swap_info& sinfo, bool write_with_sorting,
                                         common::execution_context& context) requires(std::is_pointer_v<T>) {
  if (!sinfo.is_swappable()) {
    // no swap is still kind of success, but without returning size on disk
    return std::nullopt;
  }

  if constexpr (std::is_same_v<T, cel_string_t>) {
    if (write_with_sorting) {
      io::unsorted_string_pointer_byte_iterator pointer_it{std::span{pointers.data},
                                                           reinterpret_cast<uintptr_t>(buffer.data.data())};
      return sinfo.storage_manager().compress_and_write_stream(swap_file, pointer_it, pointers.data.byte_size(), sinfo,
                                                               io::get_type_id<T>(), context);
    }
  }
  io::pointer_byte_iterator<T> pointer_it{std::span{pointers.data}, reinterpret_cast<uintptr_t>(buffer.data.data())};
  return sinfo.storage_manager().compress_and_write_stream(swap_file, pointer_it, pointers.data.byte_size(), sinfo,
                                                           io::get_type_id<T>(), context);
}

template <typename T>
std::optional<size_t> write_out_buffer(const loaded_data<T>& pointers,
                                       const loaded_data<std::remove_const_t<std::remove_pointer_t<T>>>& buffer,
                                       const std::string& buffer_swap_file, const swap_info& sinfo,
                                       bool write_with_sorting, common::execution_context& context) {
  if (!sinfo.is_swappable()) {
    return std::nullopt;
  }

  if constexpr (std::is_same_v<T, cel_string_t>) {
    if (write_with_sorting) {
      io::unsorted_string_buffer_byte_iterator iter{std::span{pointers.data}, std::span{buffer.data}};
      return sinfo.storage_manager().compress_and_write_stream(
          buffer_swap_file, iter, buffer.data.byte_size(), sinfo,
          io::get_type_id<std::remove_const_t<std::remove_pointer_t<T>>>(), context);
    }
  }

  io::byte_iterator iter{io::create_byte_iterator_from(std::span{buffer.data})};
  return sinfo.storage_manager().compress_and_write_stream(
      buffer_swap_file, iter, buffer.data.byte_size(), sinfo,
      io::get_type_id<std::remove_const_t<std::remove_pointer_t<T>>>(), context);
}
#endif

}  // namespace details

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_data_handler(
    const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr,
    const legacy_embedded_ctl::shared_static_array<STORAGE_T>& buffer, const std::string& swap_file,
    pointer_data_handler_swap_type type, swap_info sinfo, const std::string& description) {
  std::shared_ptr<pointer_data_handler> pointer_data(
      new pointer_data_handler(ptr, buffer, sinfo, swap_file, type, description));
  return pointer_data;
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_data_handler(
    legacy_embedded_ctl::static_array<POINTER_T>&& ptr, legacy_embedded_ctl::static_array<STORAGE_T>&& buffer,
    const std::string& swap_file, pointer_data_handler_swap_type type, swap_info sinfo,
    const std::string& description) {
  legacy_embedded_ctl::shared_static_array<POINTER_T> shared_ptr(std::move(ptr));
  legacy_embedded_ctl::shared_static_array<STORAGE_T> shared_buffer(std::move(buffer));
  return create_data_handler(std::move(shared_ptr), std::move(shared_buffer), swap_file, type, sinfo, description);
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_temp_data_handler(
    const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr,
    const legacy_embedded_ctl::shared_static_array<STORAGE_T>& str_buffer) {
  return create_data_handler(ptr, str_buffer, "", pointer_data_handler_swap_type::NO_SWAP, no_swap(), "");
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_temp_data_handler(
    legacy_embedded_ctl::static_array<POINTER_T>&& ptr, legacy_embedded_ctl::static_array<STORAGE_T>&& str_buffer) {
  return create_data_handler(std::move(ptr), std::move(str_buffer), "", pointer_data_handler_swap_type::NO_SWAP,
                             no_swap(), "");
}

#ifndef CELOSTAR
template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::init_from_swap(const std::string& swap_file,
                                                                                 swap_info sinfo,
                                                                                 pointer_data_handler_swap_type type,
                                                                                 const std::string& description) {
  sinfo.memory_manager().reset();

  const std::string pointer_swap_file{swap_file + get_dict_file_ending(type).data()};
  const std::string buffer_swap_file{swap_file + get_pointer_buffer_file_ending(type).data()};

  if (data_handler::swap_file_exists(pointer_swap_file, sinfo) &&
      data_handler::swap_file_exists(buffer_swap_file, sinfo)) {
    const io::storage_manager& sm{sinfo.storage_manager()};
    const auto pointer_size_on_disk{io::file_size(pointer_swap_file, sinfo)};
    const auto buffer_size_on_disk{io::file_size(buffer_swap_file, sinfo)};

    const auto pointer_result{sm.read_compressed_mt<POINTER_T>(pointer_swap_file, sinfo, pointer_size_on_disk,
                                                               io::storage_manager::read_mode::SKIP_DATA)};
    const auto buffer_result{sm.read_compressed_mt<STORAGE_T>(buffer_swap_file, sinfo, buffer_size_on_disk,
                                                              io::storage_manager::read_mode::SKIP_DATA)};
    std::shared_ptr<pointer_data_handler> raw_data(new pointer_data_handler(pointer_result.size, buffer_result.size,
                                                                            pointer_size_on_disk, buffer_size_on_disk,
                                                                            sinfo, swap_file, type, description));
    return raw_data;
  }

  return nullptr;
}
#endif

template <typename T>
load_status pointer_data_handler<T>::get_load_status() const {
  return data_.lock_shared([](const auto& data_wrapper) {
#ifdef CELOSTAR
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<POINTER_T>&) { return load_status::LOADED; }),
#else
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<POINTER_T>&) { return load_status::LOADED; },
                                        [](const io::compressed_data&) { return load_status::COMPRESSED; },
                                        [](const swapped_data&) { return load_status::SWAPPED; }),
#endif
        data_wrapper.pointer);
  });
}

template <typename T>
load_time_t pointer_data_handler<T>::get_loaded_at() const {
  return load_time_t{loaded_at_.load()};
}

template <typename T>
persistence_status pointer_data_handler<T>::get_persistence_status() const {
  return sinfo_.persistence_state();
};

template <typename T>
bool pointer_data_handler<T>::is_swappable() const {
  return sinfo_.is_swappable();
}

template <typename T>
bool pointer_data_handler<T>::swap_file_broken() const {
  return pointer_broken_swap_file_ || buffer_broken_swap_file_;
}

#ifndef CELOSTAR
template <typename T>
bool pointer_data_handler<T>::swap_out(common::execution_context& context) {
  if (data_.lock_shared([this](const auto& data_wrapper) {
        return (std::holds_alternative<swapped_data>(data_wrapper.pointer) &&
                std::holds_alternative<swapped_data>(data_wrapper.buffer)) ||
               !sinfo_.is_swappable();
      })) {
    return true;
  }

  return data_.lock_mutable([this, &context](auto& data_wrapper) {
    if ((std::holds_alternative<swapped_data>(data_wrapper.pointer) &&
         std::holds_alternative<swapped_data>(data_wrapper.buffer)) ||
        !sinfo_.is_swappable()) {
      return true;
    }

    if (is_persisted()) {
      // Swap files are already persisted on disk so we can drop the in-memory data without having to wait anything
      data_wrapper.pointer = swapped_data{};
      data_wrapper.buffer = swapped_data{};
    } else {
      if (write_out_if_applicable(data_wrapper, context)) {
        data_wrapper.pointer = swapped_data{};
        data_wrapper.buffer = swapped_data{};
      }
    }

    return pointer_persisted_ && buffer_persisted_;
  });
}

template <typename T>
bool pointer_data_handler<T>::compress() {
  return data_.lock_mutable([write_with_sorting = write_with_sorting_](auto& data_wrapper) {
    if (std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer)) {
      if (std::get<loaded_data<POINTER_T>>(data_wrapper.pointer).data.use_count() > 1) {
        return false;
      }

      auto compressed_ptr{compress_pointers(std::get<loaded_data<POINTER_T>>(data_wrapper.pointer),
                                            std::get<loaded_data<STORAGE_T>>(data_wrapper.buffer), write_with_sorting)};
      data_wrapper.buffer = compress_buffer(std::get<loaded_data<POINTER_T>>(data_wrapper.pointer),
                                            std::get<loaded_data<STORAGE_T>>(data_wrapper.buffer), write_with_sorting);

      data_wrapper.pointer = std::move(compressed_ptr);
      return true;
    }
    return false;
  });
};
#endif

template <typename T>
const_data_accessor<T> pointer_data_handler<T>::get_const_data(const common::execution_context& context) {
  last_usage_ = std::chrono::steady_clock::now();
  usage_count_++;

  auto wait_context{context.create_sub_context("swap_in_wait_for_lock", {})};
  common::timer timer_with_lock;

  auto opt{data_.lock_shared(
      [&timer_with_lock, &wait_context](const auto& data_wrapper) -> std::optional<const_data_accessor_t> {
        if (std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer)) {
          drop_short_wait_spans(timer_with_lock, wait_context);
          return const_data_accessor_t{std::get<loaded_data<STORAGE_T>>(data_wrapper.buffer).data,
                                       std::get<loaded_data<POINTER_T>>(data_wrapper.pointer).data};
        }
        return std::nullopt;
      })};

  if (opt.has_value()) {
    return opt.value();
  }

  return data_.lock_mutable([this, &timer_with_lock, &wait_context, &context](auto& data_wrapper) {
    common::timer timer_after_lock;
    drop_short_wait_spans(timer_with_lock, wait_context);
    wait_context.get_span().finish_span();

#ifdef CELOSTAR
    legacy_embedded_debug_assert(std::holds_alternative<loaded_data<STORAGE_T>>(data_wrapper.buffer));
    legacy_embedded_debug_assert(std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer));
#else
    if (std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer)) {
      return const_data_accessor_t{std::get<loaded_data<STORAGE_T>>(data_wrapper.buffer).data,
                                   std::get<loaded_data<POINTER_T>>(data_wrapper.pointer).data};
    }

    swap_in_data(data_wrapper, timer_with_lock, timer_after_lock, context);
#endif
    return const_data_accessor_t{std::get<loaded_data<STORAGE_T>>(data_wrapper.buffer).data,
                                 std::get<loaded_data<POINTER_T>>(data_wrapper.pointer).data};
  });
}

template <typename T>
size_t pointer_data_handler<T>::get_pointer_buffer_size() const {
  return buffer_size_;
}

template <typename T>
size_t pointer_data_handler<T>::get_size() const {
  return pointer_size_;
}

template <typename T>
size_t pointer_data_handler<T>::get_size_in_memory() const {
  return data_.lock_shared([](const auto& data_wrapper) {
    return details::get_size_in_memory(data_wrapper.pointer) + details::get_size_in_memory(data_wrapper.buffer);
  });
};

template <typename T>
size_t pointer_data_handler<T>::get_size_on_disk() const {
  return pointer_size_on_disk_ + buffer_size_on_disk_;
};

template <typename T>
bool pointer_data_handler<T>::is_persisted() const {
  return pointer_persisted_ && buffer_persisted_;
}

template <typename T>
size_t pointer_data_handler<T>::get_usage_count() const {
  return usage_count_.load();
};

template <typename T>
usage_time_t pointer_data_handler<T>::get_last_usage() const {
  return usage_time_t{last_usage_.load()};
};

template <typename T>
std::thread::id pointer_data_handler<T>::get_loaded_by() const {
  return loaded_by_.load();
}

template <typename T>
std::string pointer_data_handler<T>::description() const {
  return desc_;
};

template <typename T>
pointer_data_handler<T>::~pointer_data_handler() {
#ifndef CELOSTAR
  if (sinfo_.is_swappable()) {
    if (delete_from_disk_when_destructed_.load()) {
      if (pointer_persisted_) {
        delete_swap_file(sinfo_, pointer_swap_file_);
      }
      if (buffer_persisted_) {
        delete_swap_file(sinfo_, buffer_swap_file_);
      }
    }

    common::call_and_log_unsafe_callable(
        [this]() { sinfo_.storage_manager().deregister_file(pointer_swap_file_, sinfo_, description()); },
        fmt::format("Couldn't deregister file: {}", pointer_swap_file_));
    common::call_and_log_unsafe_callable(
        [this]() { sinfo_.storage_manager().deregister_file(buffer_swap_file_, sinfo_, description()); },
        fmt::format("Couldn't deregister file: {}", buffer_swap_file_));
  }
#endif
}

#ifndef CELOSTAR
template <typename T>
bool pointer_data_handler<T>::write_out(common::execution_context& context) {
  auto wait_context{context.create_sub_context("swap_in_wait_for_lock", {})};
  common::timer timer_with_lock;

  if (is_persisted()) {
    drop_short_wait_spans(timer_with_lock, wait_context);
    return true;
  }

  return data_.lock_mutable([this, &timer_with_lock, &wait_context, &context](auto& data_wrapper) {
    common::timer timer_after_lock;
    drop_short_wait_spans(timer_with_lock, wait_context);
    wait_context.get_span().finish_span();

    if (is_persisted()) {
      return true;
    }
    return write_out_if_applicable(data_wrapper, context);
  });
}
#endif

template <typename T>
void pointer_data_handler<T>::swap_in(const common::execution_context& context) {
  auto wait_context{context.create_sub_context("swap_in_wait_for_lock", {})};
  common::timer timer_with_lock;

  auto already_swapped_in = data_.lock_shared([](const auto& data_wrapper) {
    if (std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer)) {
      common::runtime_assert(std::holds_alternative<loaded_data<STORAGE_T>>(data_wrapper.buffer),
                             "pointer is loaded while buffer is not.");
      return true;
    }
    return false;
  });

#ifdef CELOSTAR
  legacy_embedded_debug_assert(already_swapped_in);
#else
  if (already_swapped_in) {
    // nothing to do
    drop_short_wait_spans(timer_with_lock, wait_context);
    return;
  }

  data_.lock_mutable([this, &timer_with_lock, &wait_context, &context](auto& data_wrapper) {
    common::timer timer_after_lock;
    drop_short_wait_spans(timer_with_lock, wait_context);
    wait_context.get_span().finish_span();

    if (std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer)) {
      common::runtime_assert(std::holds_alternative<loaded_data<STORAGE_T>>(data_wrapper.buffer),
                             "pointer is loaded while buffer is not.");
      return;
    }

    swap_in_data(data_wrapper, timer_with_lock, timer_after_lock, context);
  });
#endif
}

template <typename T>
void pointer_data_handler<T>::set_delete_from_disk_when_destructed(bool value) {
  delete_from_disk_when_destructed_ = value;
}

#ifndef CELOSTAR
template <typename T>
void pointer_data_handler<T>::register_files() {
  if (sinfo_.is_swappable()) {
    sinfo_.storage_manager().register_file(pointer_swap_file_, sinfo_, desc_);
    sinfo_.storage_manager().register_file(buffer_swap_file_, sinfo_, desc_);
  }
}
#endif

template <typename T>
pointer_data_handler<T>::pointer_data_handler(const legacy_embedded_ctl::shared_static_array<POINTER_T>& ptr_data,
                                              const legacy_embedded_ctl::shared_static_array<STORAGE_T>& buffer_data,
                                              swap_info sinfo, const std::string& base_swap_file,
                                              pointer_data_handler_swap_type type, std::string description)
    : pointer_swap_file_{base_swap_file + get_dict_file_ending(type).data()},
      buffer_swap_file_{base_swap_file + get_pointer_buffer_file_ending(type).data()},
      pointer_size_{ptr_data.size()},
      buffer_size_{buffer_data.size()},
      data_{{loaded_data<POINTER_T>{ptr_data}, loaded_data<STORAGE_T>{buffer_data}}},
      loaded_by_{std::this_thread::get_id()},
      loaded_at_{std::chrono::steady_clock::now()},
      sinfo_{std::move(sinfo)},
      desc_{std::move(description)},
      write_with_sorting_{type == pointer_data_handler_swap_type::SWAPPED_DICTIONARY} {
#ifndef CELOSTAR
  register_files();
#endif
}

#ifndef CELOSTAR
template <typename T>
pointer_data_handler<T>::pointer_data_handler(size_t pointer_size, size_t buffer_size, size_t pointer_size_on_disk,
                                              size_t buffer_size_on_disk, swap_info sinfo,
                                              const std::string& base_swap_file, pointer_data_handler_swap_type type,
                                              std::string description)
    : pointer_size_on_disk_{pointer_size_on_disk},
      buffer_size_on_disk_{buffer_size_on_disk},
      pointer_swap_file_{base_swap_file + get_dict_file_ending(type).data()},
      buffer_swap_file_{base_swap_file + get_pointer_buffer_file_ending(type).data()},
      pointer_persisted_{true},
      buffer_persisted_{true},
      pointer_size_{pointer_size},
      buffer_size_{buffer_size},
      data_{{swapped_data{}, swapped_data{}}},
      sinfo_{std::move(sinfo)},
      desc_{std::move(description)},
      write_with_sorting_{type == pointer_data_handler_swap_type::SWAPPED_DICTIONARY} {
  register_files();
}

template <typename T>
bool pointer_data_handler<T>::write_out_if_applicable(const data_wrapper& data, common::execution_context& context) {
  return std::visit(legacy_embedded_ctl::overloaded(
                        [this, &data, &context](const loaded_data<POINTER_T>& loaded_pointer) {
                          if (loaded_pointer.data.use_count() > 1) {
                            // someone else still uses the pointers
                            return false;
                          }
                          write_out_pointers(loaded_pointer, std::get<loaded_data<STORAGE_T>>(data.buffer), context);
                          write_out_buffer(data, context);
                          return true;
                        },
                        [this, &data, &context](const io::compressed_data& compressed_pointers) {
                          write_out_pointers(compressed_pointers, context);
                          write_out_buffer(data, context);
                          return true;
                        },
                        [this, &data, &context](const swapped_data&) {
                          write_out_buffer(data, context);
                          return true;
                        }),
                    data.pointer);
}

template <typename T>
void pointer_data_handler<T>::write_out_pointers(const loaded_data<POINTER_T>& pointers,
                                                 const loaded_data<STORAGE_T>& buffer,
                                                 common::execution_context& context) {
  const auto size_on_disk_opt{
      details::write_out_pointers(pointers, buffer, pointer_swap_file_, sinfo_, write_with_sorting_, context)};
  if (size_on_disk_opt.has_value()) {
    pointer_size_on_disk_ = size_on_disk_opt.value();
    pointer_persisted_ = true;
    pointer_broken_swap_file_ = false;
  }
}

template <typename T>
void pointer_data_handler<T>::write_out_pointers(const io::compressed_data& compressed_pointers,
                                                 common::execution_context& context) {
  if (auto size_on_disk_opt{write_out_compressed_data<POINTER_T>(compressed_pointers, pointer_size_ * sizeof(POINTER_T),
                                                                 pointer_swap_file_, sinfo_, context)};
      size_on_disk_opt.has_value()) {
    pointer_size_on_disk_ = size_on_disk_opt.value();
    pointer_persisted_ = true;
    pointer_broken_swap_file_ = false;
  }
}

template <typename T>
void pointer_data_handler<T>::write_out_buffer(const data_wrapper& data, common::execution_context& context) {
  std::visit(legacy_embedded_ctl::overloaded(
                 [this, &data, &context](const loaded_data<STORAGE_T>& loaded_buffer) {
                   write_out_buffer(loaded_buffer, std::get<loaded_data<POINTER_T>>(data.pointer), context);
                 },
                 [this, &context](const io::compressed_data& compressed_buffer) {
                   write_out_buffer(compressed_buffer, context);
                 },
                 [](const swapped_data&) {}),
             data.buffer);
}

template <typename T>
void pointer_data_handler<T>::write_out_buffer(const loaded_data<STORAGE_T>& buffer,
                                               const loaded_data<POINTER_T>& pointers,
                                               common::execution_context& context) {
  if (auto size_on_disk_opt{
          details::write_out_buffer(pointers, buffer, buffer_swap_file_, sinfo_, write_with_sorting_, context)};
      size_on_disk_opt.has_value()) {
    buffer_size_on_disk_ = size_on_disk_opt.value();
    buffer_persisted_ = true;
    buffer_broken_swap_file_ = false;
  }
}

template <typename T>
void pointer_data_handler<T>::write_out_buffer(const io::compressed_data& compressed_buffers,
                                               common::execution_context& context) {
  if (auto size_on_disk_op{write_out_compressed_data<STORAGE_T>(compressed_buffers, buffer_size_ * sizeof(STORAGE_T),
                                                                buffer_swap_file_, sinfo_, context)};
      size_on_disk_op.has_value()) {
    buffer_size_on_disk_ = size_on_disk_op.value();
    buffer_persisted_ = true;
    buffer_broken_swap_file_ = false;
  }
}
#endif

template <typename T>
void pointer_data_handler<T>::swap_in_data(data_wrapper& data, common::timer& timer_with_lock,
                                           common::timer& timer_after_lock, const common::execution_context& context) {
  swap_in_buffer(data.buffer, context);
  swap_in_pointers(data.pointer, std::get<loaded_data<STORAGE_T>>(data.buffer), context);

  loaded_by_ = std::this_thread::get_id();
  loaded_at_ = std::chrono::steady_clock::now();
  log_warning_if_swap_in_too_long(timer_with_lock, timer_after_lock, desc_);
}

template <typename T>
void pointer_data_handler<T>::swap_in_pointers(data_handler_data<POINTER_T>& pointer,
                                               const loaded_data<STORAGE_T>& buffer,
                                               const common::execution_context& context) {
#ifdef CELOSTAR
  legacy_embedded_debug_assert(std::holds_alternative<loaded_data<POINTER_T>>(pointer));
  return;
#else
  if (std::holds_alternative<loaded_data<POINTER_T>>(pointer)) {
    return;
  }

  load_status status{std::holds_alternative<io::compressed_data>(pointer) ? load_status::COMPRESSED
                                                                          : load_status::SWAPPED};
  auto swap_context = create_swap_in_context(
      context, status, pointer_swap_file_, desc_, pointer_size_ * sizeof(POINTER_T), pointer_size_on_disk_,
      status == load_status::COMPRESSED ? std::get<io::compressed_data>(pointer).size() : 0);

  loaded_data<POINTER_T> loaded_pointer =
      std::visit(legacy_embedded_ctl::overloaded(
                     [](const loaded_data<POINTER_T>&) {
                       legacy_embedded_ctl::assert_unreachable();
                       return loaded_data<POINTER_T>{};
                     },
                     [&buffer, this](const io::compressed_data& compressed_pointer) {
                       return decompress_pointers<POINTER_T>(compressed_pointer, pointer_size_, buffer);
                     },
                     [&, this](const swapped_data&) {
                       auto loaded_pointer{swap_in_impl<POINTER_T>(pointer_swap_file_, pointer_size_,
                                                                   pointer_size_on_disk_, pointer_broken_swap_file_)};
                       convert_to_absolute_addresses(loaded_pointer.data, buffer.data);
                       set_swap_in_read_size_tag(swap_context, pointer_size_ * sizeof(POINTER_T));
                       return loaded_pointer;
                     }),
                 pointer);

  pointer = loaded_pointer;
#endif
}

template <typename T>
void pointer_data_handler<T>::swap_in_buffer(data_handler_data<STORAGE_T>& buffer,
                                             const common::execution_context& context) {
#ifdef CELOSTAR
  legacy_embedded_debug_assert(std::holds_alternative<loaded_data<STORAGE_T>>(buffer));
  return;
#else
  if (std::holds_alternative<loaded_data<STORAGE_T>>(buffer)) {
    return;
  }

  load_status status{std::holds_alternative<io::compressed_data>(buffer) ? load_status::COMPRESSED
                                                                         : load_status::SWAPPED};
  auto swap_context = create_swap_in_context(
      context, status, buffer_swap_file_, desc_, buffer_size_ * sizeof(STORAGE_T), buffer_size_on_disk_,
      status == load_status::COMPRESSED ? std::get<io::compressed_data>(buffer).size() : 0);

  loaded_data<STORAGE_T> loaded_buffer =
      std::visit(legacy_embedded_ctl::overloaded(
                     [](const loaded_data<STORAGE_T>&) {
                       legacy_embedded_ctl::assert_unreachable();
                       return loaded_data<STORAGE_T>{};
                     },
                     [this](const io::compressed_data& compressed_buffer) {
                       return decompress_buffer<STORAGE_T>(compressed_buffer, buffer_size_);
                     },
                     [&swap_context, this](const swapped_data&) {
                       auto loaded_buffer{swap_in_impl<STORAGE_T>(buffer_swap_file_, buffer_size_, buffer_size_on_disk_,
                                                                  buffer_broken_swap_file_)};
                       set_swap_in_read_size_tag(swap_context, buffer_size_ * sizeof(STORAGE_T));
                       return loaded_buffer;
                     }),
                 buffer);
  buffer = std::move(loaded_buffer);
#endif
}

#ifndef CELOSTAR
template <typename T>
template <typename TYPE>
loaded_data<TYPE> pointer_data_handler<T>::swap_in_impl(
    const std::string& swap_file, std::atomic<size_t>& size, std::atomic<size_t>& size_on_disk,
    std::atomic<bool>& broken_swap_file) requires(std::is_same_v<TYPE, STORAGE_T> || std::is_same_v<TYPE, POINTER_T>) {
  try {
    if (!data_handler::swap_file_exists(swap_file, sinfo_)) {
      log::jerror("Could not find swap file.", {{"swap_file", swap_file}, {"description", desc_}});
      throw common::file_exception{"Could not find swap file: {}.", swap_file};
    }
    auto read_result{sinfo_.storage_manager().read_compressed_mt<TYPE>(swap_file, sinfo_, size_on_disk)};

    if (read_result.size != size) {
      log::error("broken swap file: size recorded in swap_file {} differs from size stored in memory: {} vs. {} for {}",
                 swap_file, read_result.size, size.load(), desc_);
      throw common::file_exception{
          "broken swap file: size recorded in swap_file: {} differs from size stored in memory: [{}] vs. [{}].",
          swap_file, read_result.size, size.load()};
    }
    if (read_result.size > LARGE_SWAP_FILE_THRESHOLD) {
      log::jinfo("Swapping in large file.", to_json_swap_file_info(sinfo_.get_swap_file_path(swap_file), desc_,
                                                                   read_result.size, size_on_disk.load()));
    }
    size = read_result.size;
    broken_swap_file = false;
    return loaded_data<TYPE>{std::move(read_result.data)};
  } catch (legacy_embedded_ctl::short_of_memory& e) {
    throw;
  } catch (legacy_embedded_ctl::bad_alloc& e) {
    throw;
  } catch (std::exception& e) {
    broken_swap_file = true;
    throw;
  }
}
#endif

template <typename T>
auto pointer_data_handler<T>::get_buffer_start() const -> const STORAGE_T* {
#ifdef CELOSTAR
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<STORAGE_T>& loaded) { return loaded.data.get(); }),
        data_wrapper.buffer);
  });
#else
  static const STORAGE_T never_used{};
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<STORAGE_T>& loaded) { return loaded.data.get(); },
                                        [](const io::compressed_data&) {
                                          legacy_embedded_ctl::assert_unreachable();
                                          return &never_used;
                                        },
                                        [](const swapped_data&) {
                                          legacy_embedded_ctl::assert_unreachable();
                                          return &never_used;
                                        }),
        data_wrapper.buffer);
  });
#endif
}

template <typename T>
size_t pointer_data_handler<T>::get_buffer_size() const {
  return buffer_size_;
}

template <typename T>
auto pointer_data_handler<T>::get_pointer_start() const -> const POINTER_T* {
#ifdef CELOSTAR
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<POINTER_T>& loaded) { return loaded.data.get(); }),
        data_wrapper.pointer);
  });
#else
  static const POINTER_T never_used{};
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(
        legacy_embedded_ctl::overloaded([](const loaded_data<POINTER_T>& loaded) { return loaded.data.get(); },
                                        [](const io::compressed_data&) {
                                          legacy_embedded_ctl::assert_unreachable();
                                          return &never_used;
                                        },
                                        [](const swapped_data&) {
                                          legacy_embedded_ctl::assert_unreachable();
                                          return &never_used;
                                        }),
        data_wrapper.pointer);
  });
#endif
}

template <typename T>
size_t pointer_data_handler<T>::get_pointer_size() const {
  return pointer_size_;
}

template class pointer_data_handler<cel_string_t>;
template class pointer_data_handler<trace_type>;

}  // namespace celonis::accelerator::memory::management
