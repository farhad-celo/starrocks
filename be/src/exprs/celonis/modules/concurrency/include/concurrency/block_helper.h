#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/math.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::concurrency {

class block_helper {
 public:
  explicit block_helper(row_id block_size, row_id num_items) : block_size{block_size}, num_items{num_items} {}

  const row_id block_size;
  const row_id num_items;
  const row_id num_blocks{legacy_embedded_ctl::div_round_up(num_items, block_size)};

  template <typename F>
  void parallel_for_each(F&& f) {
    tbb::parallel_for<row_id>(0, num_blocks, [&](row_id block_id) {
      const row_id begin{block_id * block_size};
      const row_id end{block_id != num_blocks - 1 ? begin + block_size : num_items};
      f(block_id, begin, end);
    });
  }

  template <typename R, typename F>
  std::vector<R> parallel_prefix_sum(R initial_value, F&& f) {
    std::vector<R> result(std::max<row_id>(num_blocks, 0) +
                          1L);  // std::max(..., 0) to suppress null dereference error.
    result[0] = initial_value;
    parallel_for_each(
        [&](row_id block_id, row_id begin, row_id end) { f(block_id, begin, end, result[block_id + 1L]); });
    for (size_t i = 1; i < result.size(); ++i) {
      result[i] += result[i - 1L];
    }
    return result;
  }
};

}  // namespace celonis::accelerator::concurrency
