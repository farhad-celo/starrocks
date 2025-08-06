#pragma once

#include "unfolding_entities_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

/**
 * Compare function returns true if lhs < rhs where the "<" relation
 * is the total adequate order defined on the paper
 *    "Esparza, Javier & Römer, Stefan & Vogler, Walter. (2002).
 *    An Improvement of McMillan's Unfolding Algorithm."
 *
 * (Definition from the paper)
 * Loosely speaking, we say that C1 < C2 (where both are local configurations) if
 *    |C1| < |C2|, or
 *    |C1| = |C2| and C1 LEXICOGRAPHICALLY SMALLER than C2, or
 *    C1 LEXICOGRAPHICALLY EQUAL to C2 and C1.foata_normal_form < C2.foata_normal_form
 */
struct total_adequate_order {
  [[nodiscard]] bool operator()(unfolding_event& lhs, unfolding_event& rhs) const;
};

/** Invert order to build a min heap*/
struct min_heap_total_adequate_order {
  [[nodiscard]] bool operator()(unfolding_event* lhs, unfolding_event* rhs) const;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
