#pragma once

#include <ctl/static_array.h>

#include "modules/operators/process/align_model/align_model_types.h"

namespace celonis::accelerator::operators::process::align_model {

/*These are the 6 deviation categories that are currently defined.
 * A deviation category always applied to an (activity,move) pair.
 * All equal pairs in a trace will belong to the same deviation category, i.e. if there is one (A,MODEL_MOVE) classified
 * in a trace that is classified as OUT_OF_SEQUENCE all other (A,MODEL_MOVE) in that trace will also be classified as
 * such.
 * These categories are surjective, i.e. every (activity,move) is assigned to exactly one category.
 * The conditions for these categories are explained below
 */
enum class deviation_category : std::uint8_t {
  CONFORMING = 1,       // is sync or gateway move
  EXCESSIVE = 2,        // is log move & there is at least one sync move
  MISSING = 3,          // is model move & trace has no log moves
  OUT_OF_SEQUENCE = 4,  // is log or model move & trace has both log moves and model moves
  UNDESIRED = 5,        // is log move & trace has no sync moves
  UNMAPPED = 6          // is unmapped move
};

using deviation_categories_for_case_t = ctl::static_array<deviation_category>;
using deviation_categories_for_cases_t = ctl::static_array<deviation_categories_for_case_t>;
using deviation_categories_for_cases_view_t = ctl::array_view<const deviation_categories_for_case_t>;

[[nodiscard]] constexpr static std::string_view deviation_category_to_string(deviation_category category) {
  switch (category) {
    case deviation_category::CONFORMING:
      return "CONFORMING";
    case deviation_category::EXCESSIVE:
      return "EXCESSIVE";
    case deviation_category::MISSING:
      return "MISSING";
    case deviation_category::OUT_OF_SEQUENCE:
      return "OUT_OF_SEQUENCE";
    case deviation_category::UNDESIRED:
      return "UNDESIRED";
    case deviation_category::UNMAPPED:
      return "UNMAPPED";
    default:
      ctl::assert_unreachable();
  }
}

/**
 * @brief Computes the deviation categories per move in each alignment in `alignments`. The result has the same
 * dimensions as `alignments`. If the `std::optional<alignment_t>` in the source range is nullopt, the corresponding
 * static array will have size 0.
 *
 * @return deviation_categories_t An array array of the same size as `alignments`.
 * Each element has the same size as the alignment of that index and contain a deviation category for each move in that
 * alignment.
 */
deviation_categories_for_cases_t compute_categories(const alignments_t& alignments);

}  // namespace celonis::accelerator::operators::process::align_model