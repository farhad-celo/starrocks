#pragma once

#include <ctl/checked_ptr.h>

namespace celonis::accelerator::memory {

class column_processing_state;

class column;
using column_t = ctl::checked_shared_ptr<column>;

struct col_cache_key;

// column pointers
template <class COL_PTRS_TYPE>
class column_ptrs_impl;

template <class COL_PTRS_TYPE>
class raw_column_ptrs_impl;

template <class COL_PTRS_TYPE>
class raw_immutable_column_ptrs_impl;

}  // namespace celonis::accelerator::memory
