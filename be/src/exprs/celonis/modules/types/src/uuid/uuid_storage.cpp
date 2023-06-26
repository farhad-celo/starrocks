#include "types/uuid/uuid_storage.h"

namespace celonis::accelerator::types::uuid {

size_t uuid_storage::compute_hash() const {
  uint64_t front{0};
  std::memcpy(&front, uuid_.data(), 8);
  uint64_t back{0};
  std::memcpy(&back, uuid_.data() + 8, 8);
  size_t hash{std::hash<uint64_t>()(front)};
  boost::hash_combine(hash, std::hash<uint64_t>()(back));
  return hash;
}

std::string uuid_storage::to_string() const {
  std::string uuid_string;
  uuid_string.reserve(UUID_STRING_LENGTH);

  for (size_t i{0}; i < uuid_.size(); i++) {
    uuid_string.push_back(HEX_CHARS[uuid_[i] >> 4]);
    uuid_string.push_back(HEX_CHARS[uuid_[i] & 0x0F]);

    if (i == 3 || i == 5 || i == 7 || i == 9) {
      uuid_string.push_back(UUID_SEPARATOR);
    }
  }

  return uuid_string;
}

}  // namespace celonis::accelerator::types::uuid