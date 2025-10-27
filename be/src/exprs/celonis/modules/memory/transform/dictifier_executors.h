#pragma once

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/bitset_base.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/row_id.h"
#include "modules/memory/transform/dictifier_types.h"
#include "modules/memory/transform/parallel_hash_table.h"

namespace celonis::accelerator::memory::transform::details {

template <typename TYPE>
class exec_dictify_step2 final {
 public:
  exec_dictify_step2(const std::vector<dictify_work_item>& work_items,
                     legacy_embedded_ctl::static_array<TYPE>& distinct_values,
                     const legacy_embedded_ctl::bitset_view_t null_flags, const row_id size,
                     const row_id non_null_value_count, std::pair<TYPE, row_id>* index_prepared,
                     const common::execution_context& context)
      : work_items(work_items),
        distinct_values(distinct_values),
        null_flags(null_flags),
        size(size),
        non_null_value_count(non_null_value_count),
        index_prepared(index_prepared),
        context{context} {}

  template <typename COL_PTRS_TYPE>
  raw_column_ptrs_t operator()() const {
    auto raw_column_pointers = create_raw_column_pointer<COL_PTRS_TYPE>(size, memory::zero_init_t{false}, context);
    auto col_ptrs = raw_column_pointers->get_data();

    tbb::parallel_for(size_t{0}, work_items.size(), [this, &col_ptrs](const size_t w) {
      const dictify_work_item item = this->work_items[w];

      // The cast avoids the calls to the pair default constructor
      auto actual_dict{memory::tracking::make_static_array<std::pair<TYPE, row_id>>(
          static_cast<size_t>(item.value_count), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG),
          context)};

      // this maps values to RIDs - actual_dict_ptr points to the current value.
      std::pair<TYPE, row_id>* actual_dict_ptr = actual_dict.data();
      // write the result back to the target column and fill the actual_dict
      // the actual_dict will also be used for scan operations
      row_id value_index = item.start_value_count;
      // index_prepared is the pre-ordered index of values.
      std::pair<TYPE, row_id>* index_prepared_ptr = &this->index_prepared[item.start];

      // Special treatment for first work item
      if (w == 0) {
        // initialize the NULL value dictionary entry
        // TODO(unknown) we need to put some value into the ptr variable. Right now we just use the first value
        // in the RS.
        std::pair<TYPE, row_id> null_entry;
        null_entry.second = 0;
        *actual_dict_ptr = null_entry;
        actual_dict_ptr++;
        value_index++;
      }

      if ((work_items[w].value_count > 0 && w > 0) || (w == 0 && work_items[0].value_count > 1)) {
        // initialize the first non_null value.
        TYPE next_value = index_prepared_ptr->first;

        actual_dict_ptr->first = next_value;
        actual_dict_ptr->second = value_index;

        col_ptrs[index_prepared_ptr->second] = static_cast<COL_PTRS_TYPE>(value_index);

        index_prepared_ptr++;

        for (row_id i = item.start + 1; i < item.end; i++) {
          if (this->index_prepared[i].first != next_value) {
            value_index++;
            next_value = this->index_prepared[i].first;
            actual_dict_ptr++;
            actual_dict_ptr->first = next_value;
            actual_dict_ptr->second = value_index;
          }
          col_ptrs[index_prepared_ptr->second] = static_cast<COL_PTRS_TYPE>(value_index);
          index_prepared_ptr++;
        }
      }
      // we write into the memory directly to avoid pointer indirection.
      TYPE* actual_data_memory{distinct_values.data()};
      for (row_id i = 0; i < item.value_count; i++) {
        actual_data_memory[i + item.start_value_count] = actual_dict[i].first;
      }
    });

    if (this->size != this->non_null_value_count) {
      tbb::parallel_for(tbb::blocked_range<row_id>{0, this->size, 100000}, [&col_ptrs, this](const auto range) {
        null_flags.apply_in_range([&col_ptrs](const size_t i) { col_ptrs[i] = 0; }, range.begin(), range.end());
      });
    }

    return raw_column_pointers;
  }

 private:
  const std::vector<dictify_work_item>& work_items;
  legacy_embedded_ctl::static_array<TYPE>& distinct_values;
  const legacy_embedded_ctl::bitset_view_t null_flags;
  row_id size;
  row_id non_null_value_count;
  std::pair<TYPE, row_id>* index_prepared;
  const common::execution_context& context;
};

// TODO(lwolf): convert to templated lambda once we have C++20 support
template <typename DATA>
class dictionary_id_resolver final {
  using hash_table_t = parallel_hash_table<DATA>;

 public:
  dictionary_id_resolver(std::span<typename hash_table_t::hash_entry*> row_to_map_entry, const size_t block_size,
                         const common::execution_context& context)
      : row_to_map_entry_{row_to_map_entry}, block_size{block_size}, context{context} {}

  template <typename COL_PTRS_TYPE>
  raw_column_ptrs_t operator()() const {
    const auto row_count{static_cast<row_id>(row_to_map_entry_.size())};

    auto raw_column_pointers = create_raw_column_pointer<COL_PTRS_TYPE>(row_count, memory::zero_init_t{false}, context);
    auto column_pointers{raw_column_pointers->get_data()};

    tbb::parallel_for(tbb::blocked_range<row_id>{0, row_count, block_size}, [this, &column_pointers](const auto range) {
      for (row_id row = range.begin(); row < range.end(); row++) {
        const auto* entry = this->row_to_map_entry_[row];
        column_pointers[row] = entry ? static_cast<COL_PTRS_TYPE>(entry->value) : 0;
      }
    });

    return raw_column_pointers;
  }

 private:
  const std::span<typename hash_table_t::hash_entry*> row_to_map_entry_;
  const size_t block_size;
  const common::execution_context& context;
};

class exec_dictify_sort_based_str_step2 final {
 public:
  exec_dictify_sort_based_str_step2(
      const row_id rows, const legacy_embedded_ctl::static_array<std::pair<cel_string_t, row_id>>& sorted_strings,
      const legacy_embedded_ctl::bitset_view_t null_flags, const std::vector<distinct_element_data::block_data>& blocks,
      const row_id block_size, const common::execution_context& context)
      : rows(rows),
        sorted_strings(sorted_strings),
        null_flags(null_flags),
        blocks(blocks),
        block_size(block_size),
        context{context} {}

  template <typename COL_PTRS_TYPE>
  raw_column_ptrs_t operator()() const {
    auto raw_column_pointers = create_raw_column_pointer<COL_PTRS_TYPE>(rows, memory::zero_init_t{true}, context);
    auto col_ptrs = raw_column_pointers->get_data();

    tbb::parallel_for(size_t{0}, blocks.size(), [this, &col_ptrs](const size_t block_index) {
      const auto& current_block = blocks[block_index];

      const size_t block_offset = block_index * block_size;
      row_id dictionary_index = current_block.distinct_element_offset - 1;
      const auto& im = current_block.distinct_item_marker;

      for (size_t i = 0; i < im.size(); i++) {
        if (im[i]) {
          ++dictionary_index;
        }
        col_ptrs[sorted_strings[block_offset + i].second] = static_cast<COL_PTRS_TYPE>(dictionary_index);
      }
    });

    tbb::parallel_for(tbb::blocked_range<row_id>{0, static_cast<row_id>(null_flags.size()), block_size},
                      [this, &col_ptrs](const auto rng) {
                        null_flags.apply_in_range([&col_ptrs](const size_t i) { col_ptrs[i] = 0; }, rng.begin(),
                                                  rng.end());
                      });

    return raw_column_pointers;
  }

 private:
  row_id rows;
  const legacy_embedded_ctl::static_array<std::pair<cel_string_t, row_id>>& sorted_strings;
  const legacy_embedded_ctl::bitset_view_t null_flags;
  const std::vector<distinct_element_data::block_data>& blocks;
  const size_t block_size;
  const common::execution_context& context;
};

}  // namespace celonis::accelerator::memory::transform::details
