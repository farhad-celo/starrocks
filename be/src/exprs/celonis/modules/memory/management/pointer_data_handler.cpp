#include "pointer_data_handler.h"

#include <memory>

#include <ctl/assert.h>
#include <ctl/static_array.h>
#include <ctl/utility.h>

#include "log/log.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types.h"
#include "modules/common/timer.h"
#include "modules/common/trace_types.h"

namespace celonis::accelerator::memory::management {

namespace details {
// The following functions have to be put into the details namespace so that they are not shaded by the
// member functions with the same names

template <typename T>
size_t get_size_in_memory(const data_handler_data<T>& data) {
  return std::visit(ctl::overloaded([](const loaded_data<T>& loaded) { return loaded.data.byte_size(); }), data);
}

}  // namespace details

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_data_handler(
    const ctl::shared_static_array<POINTER_T>& ptr, const ctl::shared_static_array<STORAGE_T>& buffer,
    pointer_data_handler_swap_type type, const std::string& description) {
  std::shared_ptr<pointer_data_handler> pointer_data(new pointer_data_handler(ptr, buffer, type, description));
  return pointer_data;
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_data_handler(
    ctl::static_array<POINTER_T>&& ptr, ctl::static_array<STORAGE_T>&& buffer, pointer_data_handler_swap_type type,
    const std::string& description) {
  ctl::shared_static_array<POINTER_T> shared_ptr(std::move(ptr));
  ctl::shared_static_array<STORAGE_T> shared_buffer(std::move(buffer));
  return create_data_handler(std::move(shared_ptr), std::move(shared_buffer), type, description);
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_temp_data_handler(
    const ctl::shared_static_array<POINTER_T>& ptr, const ctl::shared_static_array<STORAGE_T>& str_buffer) {
  return create_data_handler(ptr, str_buffer, pointer_data_handler_swap_type::NO_SWAP, "");
}

template <typename T>
std::shared_ptr<pointer_data_handler<T>> pointer_data_handler<T>::create_temp_data_handler(
    ctl::static_array<POINTER_T>&& ptr, ctl::static_array<STORAGE_T>&& str_buffer) {
  return create_data_handler(std::move(ptr), std::move(str_buffer), pointer_data_handler_swap_type::NO_SWAP, "");
}

template <typename T>
load_status pointer_data_handler<T>::get_load_status() const {
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(ctl::overloaded([](const loaded_data<POINTER_T>&) { return load_status::LOADED; }),
                      data_wrapper.pointer);
  });
}

template <typename T>
load_time_t pointer_data_handler<T>::get_loaded_at() const {
  return load_time_t{loaded_at_.load()};
}

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

    debug_assert(std::holds_alternative<loaded_data<STORAGE_T>>(data_wrapper.buffer));
    debug_assert(std::holds_alternative<loaded_data<POINTER_T>>(data_wrapper.pointer));
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
pointer_data_handler<T>::~pointer_data_handler() = default;

template <typename T>
void pointer_data_handler<T>::set_delete_from_disk_when_destructed(bool value) {
  delete_from_disk_when_destructed_ = value;
}

template <typename T>
pointer_data_handler<T>::pointer_data_handler(const ctl::shared_static_array<POINTER_T>& ptr_data,
                                              const ctl::shared_static_array<STORAGE_T>& buffer_data,
                                              pointer_data_handler_swap_type type, std::string description)
    : pointer_size_{ptr_data.size()},
      buffer_size_{buffer_data.size()},
      data_{{loaded_data<POINTER_T>{ptr_data}, loaded_data<STORAGE_T>{buffer_data}}},
      loaded_by_{std::this_thread::get_id()},
      loaded_at_{std::chrono::steady_clock::now()},
      desc_{std::move(description)},
      write_with_sorting_{type == pointer_data_handler_swap_type::SWAPPED_DICTIONARY} {}

template <typename T>
auto pointer_data_handler<T>::get_buffer_start() const -> const STORAGE_T* {
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(ctl::overloaded([](const loaded_data<STORAGE_T>& loaded) { return loaded.data.get(); }),
                      data_wrapper.buffer);
  });
}

template <typename T>
size_t pointer_data_handler<T>::get_buffer_size() const {
  return buffer_size_;
}

template <typename T>
auto pointer_data_handler<T>::get_pointer_start() const -> const POINTER_T* {
  return data_.lock_shared([](const auto& data_wrapper) {
    return std::visit(ctl::overloaded([](const loaded_data<POINTER_T>& loaded) { return loaded.data.get(); }),
                      data_wrapper.pointer);
  });
}

template <typename T>
size_t pointer_data_handler<T>::get_pointer_size() const {
  return pointer_size_;
}

template class pointer_data_handler<cel_string_t>;
template class pointer_data_handler<trace_type>;

}  // namespace celonis::accelerator::memory::management
