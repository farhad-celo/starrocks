#include "modules/memory/transform/dictifier.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

#include <ctl/assert.h>
#include <ctl/bitset.h>
#include <ctl/static_array.h>
#include <ctl/static_array_fwd.h>

#include "log/log.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/shared_types.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/dict_compare.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/raw_dictionary.h"
#include "modules/memory/row_id.h"
#include "modules/memory/transform/dictifier_executors.h"
#include "modules/memory/transform/dictifier_types.h"
#include "modules/memory/transform/parallel_hash_table.h"
#include "modules/memory/unique_value_estimation.h"

/**
 * Materialized data is here converted to dictionary data
 */

namespace celonis::accelerator::memory::transform {
namespace {
void search_and_replace_minus_zero(std::span<double> values) {
  auto it{std::lower_bound(values.begin(), values.end(), 0.0)};
  if (it != values.end() && std::fpclassify(*it) == FP_ZERO) {
    *it = std::fabs(*it);
  }
}

// The maximum ratio of unique values in a column for which the hash-based dictify path is used.
// Value chosen based on microbenchmark runtime results.
constexpr double MAX_UNIQUE_RATIO{0.3};
constexpr double LOGGING_PROBABILITY{0.001};
constexpr size_t BLOCK_SIZE{1 << 20};

// helper structs
using dictify_work_item = details::dictify_work_item;
using distinct_element_data = details::distinct_element_data;

// executors
using exec_dictify_sort_based_str_step2 = details::exec_dictify_sort_based_str_step2;

// Returns a copy of the column containing only the non null elements with their original column index
ctl::static_array<std::pair<cel_string_t, row_id>> create_non_null_data(const cel_string_t* data,
                                                                        const row_id row_count,
                                                                        const ctl::bitset_view_t null_flags,
                                                                        const row_id block_count, size_t block_size,
                                                                        const common::execution_context& context) {
  struct unsorted_block_data {
    row_id non_null_element_count = 0;
    row_id non_null_offset = 0;
  };
  std::vector<unsorted_block_data> unsorted_info(block_count);

  // counts the amount of non null elements in every block
  tbb::parallel_for<row_id>(0, static_cast<row_id>(unsorted_info.size()),
                            [row_count, block_size, &unsorted_info, &null_flags](row_id item) {
                              auto& block = unsorted_info[item];

                              const row_id start = static_cast<row_id>(block_size) * item;
                              const row_id end = std::min(row_count, start + static_cast<row_id>(block_size));
                              const auto num_null_elements{static_cast<row_id>(null_flags.count(start, end))};
                              block.non_null_element_count = (end - start) - num_null_elements;
                            });

  row_id non_null_element_count = unsorted_info[0].non_null_element_count;
  for (size_t i = 1; i < unsorted_info.size(); i++) {
    unsorted_info[i].non_null_offset = non_null_element_count;
    non_null_element_count += unsorted_info[i].non_null_element_count;
  }

  auto index_prepared{ctl::make_static_array_for_overwrite<std::pair<cel_string_t, row_id>>(
      non_null_element_count, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  // copies the non null elements into index_prepared
  tbb::parallel_for<row_id>(0, static_cast<row_id>(unsorted_info.size()),
                            [data, row_count, block_size, &unsorted_info, &index_prepared, &null_flags](row_id item) {
                              const auto& block = unsorted_info[item];

                              const row_id start = static_cast<row_id>(block_size) * item;
                              const row_id end = std::min(row_count, start + static_cast<row_id>(block_size));
                              row_id index_prepared_index = block.non_null_offset;
                              null_flags.apply_on_unset_in_range(
                                  [&](const size_t i) {
                                    index_prepared[index_prepared_index] = std::pair<cel_string_t, row_id>(data[i], i);
                                    index_prepared_index++;
                                  },
                                  start, end);
                            });

  return index_prepared;
}

// Scans the sorted data to detect the amount of distinct elements and buffer size.
// The results are returned for every block and for the whole column
distinct_element_data scan_sorted_data(ctl::static_array<std::pair<cel_string_t, row_id>>& index_prepared,
                                       const row_id block_count, const row_id block_size) {
  // Each block covers a range in the index_prepared array.
  std::vector<typename distinct_element_data::block_data> sorted_info(block_count);

  if (index_prepared.empty()) {
    return distinct_element_data{1, NULL_STRING.size(), std::move(sorted_info)};
  }

  // For each block its distinct_element_count is calculated and the buffer_size needed for the distinct elements
  // These values are to be done to create the dictionary.
  tbb::parallel_for<row_id>(
      0, static_cast<row_id>(sorted_info.size()), [block_size, &index_prepared, &sorted_info](row_id item) {
        auto& current_block = sorted_info[item];

        row_id start = block_size * item;
        const row_id end = std::min(static_cast<row_id>(index_prepared.size()), start + block_size);
        current_block.distinct_item_marker.resize(end - start);

        // The very first element is always distinct but this can not be detected in the following for loop
        // since it can not be compared with its previous element. For this reason some special treatment is applied.
        if (start == 0) {
          current_block.distinct_element_count = 1;
          current_block.buffer_size += strlen(index_prepared[start].first) + 1;
          current_block.distinct_item_marker[start] = true;
          start = 1;
        }

        for (row_id i = start; i < end; i++) {
          if (strcmp(index_prepared[i - 1].first, index_prepared[i].first) != 0) {  // if (distinct)
            ++current_block.distinct_element_count;
            current_block.buffer_size += strlen(index_prepared[i].first) + 1;
            current_block.distinct_item_marker[i - block_size * item] = true;
          }
        }
      });

  // The values for each block are accumulated to retrieve a value for the whole column
  // This is done with respect to the NULL element
  row_id total_distinct_element_count = 1;
  size_t total_buffer_size = NULL_STRING.size();

  for (auto& info : sorted_info) {
    info.distinct_element_offset = total_distinct_element_count;
    info.byte_buffer_offset = total_buffer_size;

    total_distinct_element_count += info.distinct_element_count;
    total_buffer_size += info.buffer_size;
  }

  return distinct_element_data{total_distinct_element_count, total_buffer_size, std::move(sorted_info)};
}

template <typename T>
raw_dictionary_and_pointers make_null_dictionary(const row_id row_count, const size_t block_size,
                                                 const common::execution_context& context) {
  auto raw_column_pointers{
      create_raw_column_pointer<col_ptr_binary_domain>(row_count, memory::zero_init_t{false}, context)};
  tbb::parallel_for(tbb::blocked_range<row_id>(0, row_count, block_size), [&](tbb::blocked_range<row_id> range) {
    auto next_it{std::next(raw_column_pointers->get_data().begin(), range.begin())};
    std::fill_n(next_it, range.size(), 0);
  });

  raw_dictionary_t raw_dictionary;
  auto values{ctl::make_static_array_value_init<T>(1, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
  if constexpr (std::is_same_v<T, cel_string_t>) {
    ctl::static_array<char> string_buffer{
        ctl::make_static_array_for_overwrite<char>(NULL_STRING.size(), ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
    strncpy(string_buffer.data(), NULL_STRING.data(), NULL_STRING.size());
    values[0] = string_buffer.data();
    raw_dictionary = std::make_unique<typed_raw_dictionary<cel_string_t>>(std::move(values), std::move(string_buffer));
  } else {
    raw_dictionary = std::make_unique<typed_raw_dictionary<T>>(std::move(values));
  }
  return {std::move(raw_dictionary), std::move(raw_column_pointers)};
}

void partition(std::vector<dictify_work_item>& work_items, const row_id row_count, const row_id first = 0) {
  const row_id work_items_size = static_cast<row_id>(work_items.size());
  for (row_id i = 0; i < work_items_size - 1; i++) {
    work_items[i].start = i * (row_count / work_items_size) + first;
    work_items[i].end = (i + 1) * (row_count / work_items_size) + first;
    work_items[i].value_count = 0;
    work_items[i].start_value_count = 0;
  }

  work_items[work_items.size() - 1].start = (work_items_size - 1) * (row_count / work_items_size) + first;
  work_items[work_items.size() - 1].end = row_count + first;
  work_items[work_items.size() - 1].value_count = 0;
  work_items[work_items.size() - 1].start_value_count = 0;
}

}  // namespace

namespace details {

template <typename TYPE>
raw_dictionary_and_pointers dictify_impl(std::span<const TYPE> data, ctl::bitset_view_t null_flags,
                                         const size_t null_flags_count, const size_t estimated_unique_value_count,
                                         const std::string& description, common::execution_context& context,
                                         common::timer& timer) {
  raw_dictionary_and_pointers result{};
  bool use_hash_based_algorithm{estimated_unique_value_count <
                                static_cast<size_t>(MAX_UNIQUE_RATIO * static_cast<double>(data.size()))};

  bool enable_logging{false};
  if (use_hash_based_algorithm) {
    try {
      result = details::dictify_hash(data, estimated_unique_value_count, null_flags, BLOCK_SIZE, context);
    } catch (const common::internal_exception& e) {
      log::warn(
          "Hash-based dictify for column {} was aborted due to an exception: {}. We fallback to sort-based dictify.",
          description, e.internal_message());
      enable_logging = true;
      use_hash_based_algorithm = false;
      // Fall back to sort-based dictify
    }
  }
  if (!use_hash_based_algorithm) {
    result = details::dictify_sort(data, null_flags, BLOCK_SIZE, context);
  }

  timer.stop();

  auto actual_unique_value_count{result.dictionary->get_size()};
  const std::string log_message{"Dictify column"};
  legacy_embedded_format::json::json_object_t log_event{
      {{"algorithm", use_hash_based_algorithm ? "hash" : "sort"},
       {"description", description},
       {"column_type", convert_to_string(get_matching_data_type<TYPE>())},
       {"column_size", data.size()},
       {"estimated_unique_value_count", estimated_unique_value_count},
       {"actual_unique_value_count", actual_unique_value_count},
       {"runtime_ms", timer.duration().count()}}};

  if constexpr (ctl::IS_DEBUG_BUILD) {
    log::jdebug(log_message, log_event);
  } else {
    std::random_device rd{};
    std::default_random_engine prng{rd()};
    std::bernoulli_distribution logging_distribution{LOGGING_PROBABILITY};

    if (enable_logging || logging_distribution(prng)) {
      log::jinfo(log_message, log_event);
    }
  }

  auto& span{context.get_span()};
  span.set_tag("algorithm", use_hash_based_algorithm ? "hash" : "sort");
  span.set_tag("description", description);
  span.set_tag("estimated_unique_value_count", estimated_unique_value_count);
  span.set_tag("actual_unique_value_count", actual_unique_value_count);
  span.set_tag("column_size", data.size());
  span.set_tag("null_flags_count", null_flags_count);

  return result;
}

template <typename TYPE>
raw_dictionary_and_pointers dictify_sort(std::span<const TYPE> data, const ctl::bitset_view_t null_flags,
                                         const row_id block_size, const common::execution_context& context) {
  // #lizard forgives: This magic string whitelists the current function from lizard warning output.
  const auto row_count{static_cast<row_id>(data.size())};

  // partition work
  std::vector<dictify_work_item> work_items(row_count / block_size + 1);
  partition(work_items, row_count);

  // count non null values and partition output
  row_id non_null_value_count = 0;
  tbb::parallel_for(size_t{0}, work_items.size(), [&](size_t w) {
    dictify_work_item& item = work_items[w];
    item.value_count = (item.end - item.start) - static_cast<row_id>(null_flags.count(item.start, item.end));
  });
  work_items[0].start_value_count = 0;
  for (size_t i = 1; i < work_items.size(); i++) {
    work_items[i].start_value_count = work_items[i - 1].value_count + work_items[i - 1].start_value_count;
  }
  non_null_value_count = work_items.back().start_value_count + work_items.back().value_count;
  // setup data structure
  // the cast avoids calls to default constructor of the tuple
  auto index_prepared{ctl::make_static_array_for_overwrite<std::pair<TYPE, row_id>>(
      static_cast<size_t>(non_null_value_count), ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  tbb::parallel_for(uint64_t{0}, uint64_t{work_items.size()},
                    [&work_items, &index_prepared, data, &null_flags](uint64_t w) {
                      row_id curr_pos = work_items[w].start_value_count;
                      null_flags.apply_on_unset_in_range(
                          [&](const size_t i) {
                            index_prepared[curr_pos] = std::make_pair(data[i], i);
                            ++curr_pos; /* step forward if value is not null*/
                          },
                          work_items[w].start, work_items[w].end);
                    });

  tbb::parallel_sort(index_prepared.data(), index_prepared.data() + non_null_value_count, dict_compare<TYPE>{});

  // repartition the work
  work_items.resize(non_null_value_count != 0 ? non_null_value_count / block_size + 1 : 1);
  partition(work_items, non_null_value_count);

  // align start and end pointers to a value change
  tbb::parallel_for(uint64_t{0}, uint64_t{work_items.size()},
                    [&work_items, &index_prepared, non_null_value_count](uint64_t w) {
                      row_id start = work_items[w].start;
                      row_id end = work_items[w].end;
                      row_id value_count = 0;

                      if (w != 0) {
                        // find first value change
                        for (row_id s = work_items[w].start; s <= non_null_value_count; s++) {
                          start = s;
                          if (s < non_null_value_count && index_prepared[s].first != index_prepared[s - 1].first) {
                            break;
                          }
                        }
                      }
                      // find first value change after end is reached and count distinct values
                      row_id s = start;
                      if (w == 0) {
                        s++;
                        // The first work item always finds at least one value, expect if there is no non null value
                        if (non_null_value_count > 0) {
                          value_count++;
                        }
                      }
                      for (; s < non_null_value_count; s++) {
                        if (s >= work_items[w].end && s < non_null_value_count &&
                            index_prepared[s].first != index_prepared[s - 1].first) {
                          break;
                        }
                        if (index_prepared[s].first != index_prepared[s - 1].first) {
                          value_count++;
                        }
                      }
                      end = s;
                      // Special treatment for first work item
                      if (w == 0) {
                        // add null value at the beginning
                        value_count++;
                      }

                      work_items[w].start = start;
                      work_items[w].end = end;
                      work_items[w].value_count = value_count;
                    });

  row_id distinct_value_count_with_null = 0;
  // partition output
  for (auto& work_item : work_items) {
    work_item.start_value_count = distinct_value_count_with_null;
    distinct_value_count_with_null += work_item.value_count;
  }

  auto distinct_values{
      ctl::make_static_array_for_overwrite<TYPE>(distinct_value_count_with_null, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  // If there are n different values including NULL, the indices are 0..n-1. The largest value to represent is n-1.
  raw_column_ptrs_t raw_column_pointers{memory::execute_with_column_pointers_type(
      exec_dictify_step2<TYPE>(work_items, distinct_values, null_flags, row_count, non_null_value_count,
                               index_prepared.data(), context),
      distinct_value_count_with_null)};

  if constexpr (std::is_same_v<TYPE, cel_float_t>) {
    search_and_replace_minus_zero({distinct_values.begin() + 1, distinct_values.end()});
  }

  return {std::make_unique<typed_raw_dictionary<TYPE>>(std::move(distinct_values)), std::move(raw_column_pointers)};
}

template <>
raw_dictionary_and_pointers dictify_sort(std::span<const cel_string_t> data, const ctl::bitset_view_t null_flags,
                                         const row_id block_size, const common::execution_context& context) {
  const auto row_count{static_cast<row_id>(data.size())};
  // integer division of size / BLOCK_SIZE but the result is rounded up
  row_id block_count = ((row_count - 1) / block_size) + 1;

  auto index_prepared{create_non_null_data(data.data(), row_count, null_flags, block_count, block_size, context)};

  tbb::parallel_sort(index_prepared.begin(), index_prepared.end(),
                     [](const std::pair<cel_string_t, row_id>& rhs, const std::pair<cel_string_t, row_id>& lhs) {
                       return strcmp(rhs.first, lhs.first) < 0;
                     });

  block_count = ((static_cast<row_id>(index_prepared.size()) - 1) / block_size) + 1;
  distinct_element_data sorted_description = scan_sorted_data(index_prepared, block_count, block_size);

  raw_column_ptrs_t raw_column_pointers{memory::execute_with_column_pointers_type(
      exec_dictify_sort_based_str_step2{row_count, index_prepared, null_flags, sorted_description.block_info,
                                        block_size, context},
      sorted_description.total_distinct_element_count)};

  ctl::static_array<cel_string_t> distinct_values{ctl::make_static_array_for_overwrite<cel_string_t>(
      static_cast<size_t>(sorted_description.total_distinct_element_count), ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
  ctl::static_array<char> string_buffer{ctl::make_static_array_for_overwrite<char>(sorted_description.total_buffer_size,
                                                                                   ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  char* buffer = string_buffer.data();
  cel_string_t* memory_pointers = distinct_values.data();

  memory_pointers[0] = buffer;
  strncpy(buffer, NULL_STRING.data(), NULL_STRING.size());

  // The block_info vector within sorted_column of type distinct_element_data::block_data contains all the information
  // needed to copy the strings into the dictionary in parallel. In every block the byte_offset labels the first
  // address to write to and the bitset marks all distinct elements.
  tbb::parallel_for<row_id>(0, static_cast<row_id>(sorted_description.block_info.size()),
                            [&block_info = sorted_description.block_info, &index_prepared, block_size, memory_pointers,
                             buffer](row_id block_index) {
                              auto& current_block = block_info[block_index];
                              auto* buff = buffer + current_block.byte_buffer_offset;

                              const row_id block_offset = block_size * block_index;
                              row_id distinct_items_found = 0;
                              const auto& im = current_block.distinct_item_marker;

                              for (auto i = im.find_first(); i != im.npos; i = im.find_next(i)) {
                                memory_pointers[current_block.distinct_element_offset + distinct_items_found] = buff;
                                ++distinct_items_found;

                                cel_string_t str = index_prepared[block_offset + i].first;
                                auto len = strlen(str) + 1;
                                memcpy(buff, str, len);
                                buff += len;
                              }
                            });

  auto raw_dictionary =
      std::make_unique<typed_raw_dictionary<cel_string_t>>(std::move(distinct_values), std::move(string_buffer));
  return {std::move(raw_dictionary), std::move(raw_column_pointers)};
}

/*
 * This algorithm dictifies the column by removing duplicate elements first and then sorting them.
 * Its chosen by the heuristic if only a few distinct elements are expected.
 * It works the same way as dictify_parallel_hash_based with some adjustments to deal with strings
 */
template <typename T>
raw_dictionary_and_pointers dictify_hash(std::span<const T> data, const size_t estimated_unique_value_count,
                                         const ctl::bitset_view_t null_flags, const size_t block_size,
                                         const common::execution_context& context) {
  parallel_hash_table<T> unique_values{estimated_unique_value_count, context, block_size};
  auto associated_entries{unique_values.batch_insert_or_get(data, null_flags)};

  // Create a static array containing only distinct elements from the dict_data and then sort it
  auto sorted_entries{unique_values.get_entries()};
  tbb::parallel_sort(sorted_entries.begin(), sorted_entries.end(),
                     [](const typename parallel_hash_table<T>::hash_entry* left,
                        const typename parallel_hash_table<T>::hash_entry* right) { return left->key < right->key; });

  // Now that we have determined the global ordering, we can assign the final unique ids to the entries
  tbb::parallel_for(tbb::blocked_range<size_t>{0, sorted_entries.size(), block_size}, [&](const auto& range) {
    for (auto id{range.begin()}; id < range.end(); ++id) {
      // IDs are 1-based, since ID 0 is statically assigned to the column's NULL value
      sorted_entries[id]->value = id + 1;
    }
  });

  const row_id unique_value_count_including_null{static_cast<row_id>(sorted_entries.size()) + 1};
  auto raw_column_ptrs = memory::execute_with_column_pointers_type(
      dictionary_id_resolver<T>{std::span{associated_entries}, block_size, context}, unique_value_count_including_null);

  auto total_buffer_size{unique_values.get_total_buffer_size()};

  ctl::static_array<T> distinct_values{
      ctl::make_static_array_for_overwrite<T>(unique_value_count_including_null, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  if constexpr (std::is_same_v<T, cel_string_t>) {
    ctl::static_array<char> string_buffer{ctl::make_static_array_for_overwrite<char>(
        total_buffer_size + NULL_STRING.size(), ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

    char* buffer = string_buffer.data();
    cel_string_t* memory_pointers = distinct_values.data();

    memory_pointers[0] = buffer;
    buffer = strncpy(buffer, NULL_STRING.data(), NULL_STRING.size()) + NULL_STRING.size();

    for (size_t i{1}; const auto* entry : sorted_entries) {
      memory_pointers[i] = buffer;
      auto len = entry->key.size();
      buffer = strncpy(buffer, entry->key.value().str(), len) + len;
      ++i;
    }

    return {std::make_unique<typed_raw_dictionary<cel_string_t>>(std::move(distinct_values), std::move(string_buffer)),
            std::move(raw_column_ptrs)};
  } else {
    distinct_values[0] = T{};

    for (size_t i{1}; const auto* entry : sorted_entries) {
      distinct_values[i] = entry->key.value();
      ++i;
    }

    return {std::make_unique<typed_raw_dictionary<T>>(std::move(distinct_values)), std::move(raw_column_ptrs)};
  }
}

}  // namespace details

[[nodiscard]] std::pair<dictionary_t, column_ptrs_t> raw_dictionary_and_pointers::to_swappable(  // NOLINT
    const std::string& id, const std::string& description) {
  return {std::move(*dictionary).convert_to_dictionary_t_release_data(description + management::DICT_DESC),
          create_column_pointers(column_pointers, id, description)};
}

template <typename TYPE>
raw_dictionary_and_pointers dictify(std::span<const TYPE> data, const ctl::bitset_view_t null_flags,
                                    const std::string& description, common::execution_context& context) {
  common::timer timer{};

  const auto null_flags_count{null_flags.count()};

  if (null_flags_count == data.size()) {
    return make_null_dictionary<TYPE>(data.size(), BLOCK_SIZE, context);
  }

  if (data.size() <= 300'000) {  // do not use a heuristic when column size isn't that big anyway
    return details::dictify_sort(data, null_flags, BLOCK_SIZE, context);
  }

  const size_t estimated_unique_value_count{
      memory::estimate_unique_value_count(data, {null_flags.to_block_span(), null_flags.size()}, BLOCK_SIZE)};

  return details::dictify_impl(data, null_flags, null_flags_count, estimated_unique_value_count, description, context,
                               timer);
}

template raw_dictionary_and_pointers dictify(std::span<const cel_int_t> data, const ctl::bitset_view_t null_flags,
                                             const std::string& description, common::execution_context& context);
template raw_dictionary_and_pointers dictify(std::span<const cel_float_t> data, const ctl::bitset_view_t null_flags,
                                             const std::string& description, common::execution_context& context);
template raw_dictionary_and_pointers dictify(std::span<const cel_date_t> data, const ctl::bitset_view_t null_flags,
                                             const std::string& description, common::execution_context& context);
template raw_dictionary_and_pointers dictify(std::span<const cel_string_t> data, const ctl::bitset_view_t null_flags,
                                             const std::string& description, common::execution_context& context);
template raw_dictionary_and_pointers dictify(std::span<const cel_uuid_t> data, const ctl::bitset_view_t null_flags,
                                             const std::string& description, common::execution_context& context);

// Special handling of boolean columns
template <>
raw_dictionary_and_pointers dictify(std::span<const cel_boolean_t> data, const ctl::bitset_view_t null_flags,
                                    const std::string& /*description*/, common::execution_context& context) {
  const auto row_count{static_cast<row_id>(data.size())};

  ctl::static_array<cel_boolean_t> dict_vals{
      ctl::make_static_array_for_overwrite<cel_boolean_t>(3, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};
  dict_vals[0] = false;
  dict_vals[1] = false;
  dict_vals[2] = true;

  auto raw_column_pointers =
      create_raw_column_pointer<col_ptr_binary_domain>(row_count, memory::zero_init_t{false}, context);
  auto output_ptrs = raw_column_pointers->get_data();

  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, row_count, 1 << 20}, [&output_ptrs, &data, &null_flags](const auto range) {
        null_flags.apply_in_range([&output_ptrs](row_id i) { output_ptrs[i] = 0; }, range.begin(), range.end());
        null_flags.apply_on_unset_in_range(
            [&output_ptrs, &data](row_id i) {
              output_ptrs[i] = static_cast<col_ptr_binary_domain>(static_cast<col_ptr_binary_domain>(data[i]) + 1);
            },
            range.begin(), range.end());
      });

  return {std::make_unique<typed_raw_dictionary<cel_boolean_t>>(std::move(dict_vals)), std::move(raw_column_pointers)};
}

template <typename TYPE>
size_t legacy_estimate_unique_value_count(std::span<const TYPE> data, const ctl::bitset_view_t null_flags,
                                          const common::execution_context& context) {
  // The heuristic estimates the amount of distinct elements in data
  // The implementation of the heuristic is based on ideas from the following paper:
  // See Box 4.1: http://www.uvm.edu/~ngotelli/manuscriptpdfs/Chapter%204.pdf
  // Chao for replicated incidence data is used as estimator
  const auto row_count{static_cast<row_id>(data.size())};

  constexpr row_id SAMPLES{64};
  // Sample at least 1% of the input data
  constexpr double MIN_SAMPLE_RATIO{0.01};
  const row_id SAMPLE_SIZE{std::max(static_cast<row_id>(1024),
                                    static_cast<row_id>(static_cast<double>(row_count) / SAMPLES * MIN_SAMPLE_RATIO))};

  std::default_random_engine random_engine{};
  std::uniform_int_distribution<row_id> distr{0, row_count - SAMPLE_SIZE};

  using incidence_data = std::bitset<SAMPLES>;  // type is contained in sample [i]
  using key_type = std::conditional_t<std::is_same_v<TYPE, cel_string_t>, details::cel_string_key, TYPE>;
  using hash_type =
      std::conditional_t<std::is_same_v<TYPE, cel_string_t>, details::cel_string_key::hash, std::hash<TYPE>>;
  ska::bytell_hash_map<key_type, incidence_data, hash_type> unique_values{};

  for (row_id sample = 0; sample < SAMPLES; sample++) {
    row_id offset = distr(random_engine);
    for (row_id i = offset; i < SAMPLE_SIZE + offset; i++) {
      if (!null_flags.test(
              i)) {  // Null values are treated special in both algorithms, they do not contribute to diversity
        auto it = unique_values.emplace(key_type{data[i]}, incidence_data());
        it.first->second[sample] = true;
      }
    }
  }

  size_t q1{0};  // q1 is the number of different values present in exactly one sample
  size_t q2{0};  // q2 is the number of different values present in exactly two samples
  for (auto& e : unique_values) {
    switch (e.second.count()) {
      case 1:
        ++q1;
        break;

      case 2:
        ++q2;
        break;
    }
  }

  return static_cast<size_t>(                      //
      static_cast<double>(unique_values.size()) +  //
      (SAMPLES - 1) /                              //
          static_cast<double>(SAMPLES) *           //
          static_cast<double>(q1 * (q1 - 1)) /     //
          static_cast<double>(2 * (q2 + 1))        //
  );
}

// Unique estimation is also used on column pointers, not just data.
template size_t legacy_estimate_unique_value_count(std::span<const int64_t> data, const ctl::bitset_view_t null_flags,
                                                   const common::execution_context& context);
template size_t legacy_estimate_unique_value_count(std::span<const int32_t> data, const ctl::bitset_view_t null_flags,
                                                   const common::execution_context& context);
template size_t legacy_estimate_unique_value_count(std::span<const int16_t> data, const ctl::bitset_view_t null_flags,
                                                   const common::execution_context& context);
template size_t legacy_estimate_unique_value_count(std::span<const int8_t> data, const ctl::bitset_view_t null_flags,
                                                   const common::execution_context& context);
}  // namespace celonis::accelerator::memory::transform
