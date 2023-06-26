#pragma once

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include <string>

#include <boost/functional/hash.hpp>

#include "modules/common/exceptions.h"
#include "modules/common/int_types.h"

namespace celonis::accelerator::types::uuid {

static constexpr uint8_t UUID_STRING_LENGTH{36};
static constexpr uint8_t UUID_STORAGE_BYTES{16};
static constexpr std::array<const char, UUID_STORAGE_BYTES> HEX_CHARS{
    {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'}};
static constexpr char UUID_SEPARATOR{'-'};

class uuid_storage {
 public:
  /* constructors */
  /**
   * @brief constructs a uuid_storage object with all 0
   */
  uuid_storage() = default;

  /**
   * @brief constructs a uuid_storage object from c-style array
   */
  explicit uuid_storage(uint8_t data[]);

  /**
   * @brief constructs a uuid_storage object from a string in canonical format
   */
  explicit uuid_storage(const std::string& uuid_string);

  /* comparison operators */
  // NOLINTNEXTLINE(modernize-use-nullptr)
  auto operator<=>(const uuid_storage& rhs) const = default;

  /* iterators */
  [[nodiscard]] uint8_t* begin();
  [[nodiscard]] uint8_t* end();
  [[nodiscard]] const uint8_t* cbegin() const;
  [[nodiscard]] const uint8_t* cend() const;

  /* utilities */
  /**
   * @return the hash value of a uuid
   */
  [[nodiscard]] size_t compute_hash() const;

  /**
   * @return the size of a uuid
   */
  [[nodiscard]] size_t size() const;

  /**
   * @return the canonical string representation of a uuid value
   */
  [[nodiscard]] std::string to_string() const;

  /**
   * @return the actual uuid data
   */
  [[nodiscard]] std::array<uint8_t, UUID_STORAGE_BYTES> get_data() const;

 private:
  std::array<uint8_t, UUID_STORAGE_BYTES> uuid_{};
};

inline uuid_storage::uuid_storage(uint8_t data[]) { std::copy_n(data, UUID_STORAGE_BYTES, uuid_.begin()); }

inline uuid_storage::uuid_storage(const std::string& uuid_string) {
  if (uuid_string.length() != UUID_STRING_LENGTH) {
    throw common::cpm_exception{"Invalid uuid format. Expected a string of size {} but got size {}", UUID_STRING_LENGTH,
                                uuid_string.length()};
  }

  auto hex_char_to_value = [&uuid_string](size_t i) {
    char uuid_digit{uuid_string[i]};
    if (std::isxdigit(uuid_digit) == 0) {
      throw common::cpm_exception{
          "Invalid uuid format. Character {} at position {} is not a valid hexadecimal character.", uuid_digit, i};
    }
    if (std::isdigit(uuid_digit) != 0) {
      return static_cast<uint8_t>(uuid_digit - '0');
    }
    return static_cast<uint8_t>(std::tolower(uuid_digit) - 'a' + 10);
  };

  size_t i{0};
  size_t j{0};
  while (i < UUID_STRING_LENGTH) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (uuid_string[i] != UUID_SEPARATOR) {
        throw common::cpm_exception{"Invalid uuid format. Missing separator '-' at position {}.", i};
      }
      i++;
      continue;
    }

    uuid_[j] = static_cast<uint8_t>(hex_char_to_value(i) << 4 | hex_char_to_value(i + 1));
    i += 2;
    j++;
  }
}

inline uint8_t* uuid_storage::begin() { return uuid_.begin(); }

inline uint8_t* uuid_storage::end() { return uuid_.end(); }

inline const uint8_t* uuid_storage::cbegin() const { return uuid_.cbegin(); }

inline const uint8_t* uuid_storage::cend() const { return uuid_.cend(); }

inline size_t uuid_storage::size() const { return uuid_.size(); }

inline std::array<uint8_t, UUID_STORAGE_BYTES> uuid_storage::get_data() const { return uuid_; }

inline std::ostream& operator<<(std::ostream& os, uuid_storage const& uuid) { return os << uuid.to_string(); }

}  // namespace celonis::accelerator::types::uuid

namespace std {

template <>
struct hash<celonis::accelerator::types::uuid::uuid_storage> {
  size_t operator()(const celonis::accelerator::types::uuid::uuid_storage& uuid) const noexcept {
    return uuid.compute_hash();
  }
};

}  // namespace std