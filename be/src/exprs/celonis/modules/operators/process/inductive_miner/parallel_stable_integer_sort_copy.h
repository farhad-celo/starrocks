#pragma once

#include <numeric>

#include <tbb/parallel_for.h>

#include "concurrency/block_helper.h"
#include "ctl/conversion.h"

namespace celonis::accelerator::operators::process {

struct sub_range_info {
  std::vector<size_t> offsets;
};

template <typename SOURCE_IT, typename TARGET_IT, typename MAP>
void parallel_stable_integer_sort_copy(SOURCE_IT first, SOURCE_IT last, TARGET_IT out, row_id grain_size,
                                       row_id num_buckets, MAP map) {
  if (first == last) {
    return;
  }
  concurrency::block_helper blocks{grain_size, ctl::cast<row_id>(std::distance(first, last))};
  // compute the counts of each key in each block
  std::vector<std::vector<size_t>> block_bucket_sizes(blocks.num_blocks, std::vector<size_t>(num_buckets));
  blocks.parallel_for_each([&](auto block_idx, auto start_idx, auto stop_idx) {
    for (; start_idx != stop_idx; ++start_idx) {
      block_bucket_sizes[block_idx][map(first[start_idx])]++;
    }
  });
  // now, transform the counts of keys in blocks into offsets
  for (row_id block_idx{1}; block_idx != blocks.num_blocks; ++block_idx) {
    for (row_id bucket_idx{0}; bucket_idx != num_buckets; ++bucket_idx) {
      block_bucket_sizes[block_idx][bucket_idx] += block_bucket_sizes[block_idx - 1][bucket_idx];
    }
  }
  std::rotate(begin(block_bucket_sizes), prev(end(block_bucket_sizes)), end(block_bucket_sizes));
  std::move_backward(begin(block_bucket_sizes[0]), prev(end(block_bucket_sizes[0])), end(block_bucket_sizes[0]));
  block_bucket_sizes[0][0] = {};
  std::partial_sum(begin(block_bucket_sizes[0]), end(block_bucket_sizes[0]), begin(block_bucket_sizes[0]));
  for (row_id block_idx{1}; block_idx != blocks.num_blocks; ++block_idx) {
    for (row_id bucket_idx{1}; bucket_idx != num_buckets; ++bucket_idx) {
      block_bucket_sizes[block_idx][bucket_idx] += block_bucket_sizes[0][bucket_idx];
    }
  }
  // now that we have the offsets, we can directly copy to the result:
  blocks.parallel_for_each([&](auto block_idx, auto start_idx, auto stop_idx) {
    for (; start_idx != stop_idx; ++start_idx) {
      out[block_bucket_sizes[block_idx][map(first[start_idx])]++] = first[start_idx];
    }
  });
}

}  // namespace celonis::accelerator::operators::process
