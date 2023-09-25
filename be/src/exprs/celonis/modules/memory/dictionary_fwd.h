#pragma once

#include <memory>

namespace celonis::accelerator::memory {
class dictionary;
using dictionary_t = std::shared_ptr<dictionary>;
template <typename T>
class typed_dictionary;
template <typename T>
using typed_dictionary_t = std::shared_ptr<typed_dictionary<T>>;
}  // namespace celonis::accelerator::memory
