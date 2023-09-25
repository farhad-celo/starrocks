#pragma once

#include <memory>

namespace celonis::accelerator::memory {

class raw_dictionary;

using raw_dictionary_t = std::unique_ptr<raw_dictionary>;

}  // namespace celonis::accelerator::memory
