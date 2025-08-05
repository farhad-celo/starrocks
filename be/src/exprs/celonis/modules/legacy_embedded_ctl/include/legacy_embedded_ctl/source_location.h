#pragma once

#include <cstdint>
#include <experimental/source_location>
#include <string>

#include <fmt/format.h>

#include "legacy_embedded_ctl/system_constants.h"

namespace celonis::accelerator::legacy_embedded_ctl {

class source_location final {
 public:
  explicit constexpr source_location(
      std::experimental::source_location source_location = std::experimental::source_location::current()) noexcept;

  /**
   * @brief returns the line number for this instance
   */
  [[nodiscard]] constexpr std::uint32_t line() const noexcept;
  /**
   * @brief returns the file name (including the full absolute path) for this instance
   */
  [[nodiscard]] constexpr const char* file_name() const noexcept;
  /**
   * @brief returns the pure file name (without absolute full path) for this instance
   */
  [[nodiscard]] constexpr const char* file_name_without_path() const noexcept;

 private:
  std::experimental::source_location source_location_;
};

inline constexpr source_location::source_location(std::experimental::source_location source_location) noexcept
    : source_location_{source_location} {}

inline constexpr std::uint32_t source_location::line() const noexcept { return source_location_.line(); }

inline constexpr const char* source_location::file_name() const noexcept { return source_location_.file_name(); }

// TODO(n.weber): Modernize - return std::string_view, maybe extract file name with the help of std::filesystem::path
inline constexpr const char* source_location::file_name_without_path() const noexcept {
  const char* file_name_without_path{file_name()};
  for (const char* curr_char{file_name()}; *curr_char != '\0'; ++curr_char) {
    if (*curr_char == PATH_SEPARATOR && *(curr_char + 1) != '\0') {
      file_name_without_path = curr_char + 1;
    }
  }
  return file_name_without_path;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl

template <>
struct fmt::formatter<celonis::accelerator::legacy_embedded_ctl::source_location> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /** Custom {fmt} formatter. Will output the source location in the format: [hashed file name]:[line number] */
  template <typename FormatContext>
  auto format(const celonis::accelerator::legacy_embedded_ctl::source_location& src, FormatContext& ctx) {
    return fmt::format_to(ctx.out(), "{0}:{1}", src.file_name_without_path(), src.line());
  }
};
