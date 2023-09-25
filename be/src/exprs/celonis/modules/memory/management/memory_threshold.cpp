#include "memory_threshold.h"

#ifndef CELOSTAR
#include "modules/common/exceptions.h"
#include "modules/query/loading.pb.h"

namespace celonis::accelerator::memory::management {

void memory_threshold::set(const MemoryThreshold& protobuf_memory_threshold) {
  const double pb_lower = protobuf_memory_threshold.lower();
  const double pb_higher = protobuf_memory_threshold.higher();
  if (!is_valid(pb_lower, pb_higher)) {
    throw common::invalid_input_exception{
        "The given proto message contains an invalid memory threshold ({{Low: {}, High: {}}} where 0 <= Low <= High <= "
        "1 must hold). Memory threshold is not set.",
        pb_lower, pb_higher};
  }
  set(pb_lower, pb_higher);
}

}  // namespace celonis::accelerator::memory::management
#endif