#include "legacy_embedded_ctl/exception_framework.h"

#include <algorithm>
#include <random>
#include <thread>

#include <fmt/format.h>

#include "legacy_embedded_ctl/exception_traits.h"
#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::legacy_embedded_ctl {

error_id error_id::generate() {
  static constexpr int MAX_ERROR_ID{(1 << (NUMBER_OF_HEX_DIGITS_TO_USE * 4)) - 1};  // (16^#digits) - 1
  static const auto make_seed{
      [] { return std::random_device{}() + std::hash<std::thread::id>{}(std::this_thread::get_id()); }};
  static thread_local std::minstd_rand rng{make_seed()};
  return error_id{std::uniform_int_distribution<int>{0, MAX_ERROR_ID}(rng)};
}

std::string error_id::as_hex_string() const { return fmt::format("{:04x}", as_int()); }

base_exception::base_exception(std::string message, const std::string_view exception_type_as_text) {
  init(std::move(message), exception_type_as_text);
}

const char* base_exception::what() const noexcept { return error_message_.c_str(); }

std::string base_exception::internal_message() const {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return what();
#pragma GCC diagnostic pop
}

std::string base_exception::external_message() const {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return what();
#pragma GCC diagnostic pop
}

const legacy_embedded_format::json::json_object_t& base_exception::json_key_value_container() const noexcept {
  return json_key_value_container_;
}

void base_exception::add_or_overwrite(const legacy_embedded_format::json::json_object_t& json_key_value_container) {
  std::for_each(json_key_value_container.cbegin(), json_key_value_container.cend(),
                [this](const std::pair<const legacy_embedded_format::json::json_key_t&,
                                       const legacy_embedded_format::json::json_value_t&>& key_value) {
                  const auto& [key, value]{key_value};
                  add_or_overwrite(key, value);
                });
}

legacy_embedded_format::json::json_object_t& base_exception::json_exception_context() {
  const auto exception_context_entry_it{
      json_key_value_container_.try_emplace(EXCEPTION_CONTEXT_KEY, legacy_embedded_format::json::json_object_t{})
          .first};
  return std::get<legacy_embedded_format::json::json_object_t>(exception_context_entry_it->second);
}

void base_exception::init(std::string message, std::string_view exception_type_as_text) {
  static const legacy_embedded_format::json::json_key_t EXCEPTION_TYPE_KEY{"type"};
  static const legacy_embedded_format::json::json_key_t EXCEPTION_MESSAGE_KEY{"message"};
  error_message_ = std::move(message);
  add_or_overwrite(EXCEPTION_TYPE_KEY, exception_type_as_text);
  // The exception message should be part of the exception context for cases where neither internal nor external message
  // are logged, but only the exception context and some other explanatory text
  add_or_overwrite(EXCEPTION_MESSAGE_KEY, error_message_);
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
