#pragma once

#include "legacy_embedded_ctl/checked_ptr.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

class column_processing_state;

class column;
using column_t = legacy_embedded_ctl::checked_shared_ptr<column>;

struct col_cache_key;

// column pointers
template <class COL_PTRS_TYPE>
class column_ptrs_impl;

template <class COL_PTRS_TYPE>
class raw_column_ptrs_impl;

template <class COL_PTRS_TYPE>
class raw_immutable_column_ptrs_impl;

template <class PTR_TYPE>
column_ptrs_impl<PTR_TYPE>* create_tmp_column_pointers(row_id row_count);
}  // namespace celonis::accelerator::memory
