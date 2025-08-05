#include "total_adequate_order.h"

#include "legacy_embedded_ctl/assert.h"
#include "unfolding_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

namespace {

/**
 * Lexicographically compare local configurations based on the order of reference_transition
 * If A.size() < B.size() and A[0:A.size()] = B[0:A.size()], then A < B
 * Returns:
 *      -1 if lhs < rhs,
 *      0 if lhs == rhs,
 *      1 if lhs > rhs
 */
int lexicographically_compare(const local_configuration_t& lhs, const local_configuration_t& rhs) {
  const auto max_common_size{std::min(lhs.size(), rhs.size())};
  for (size_t i{0}; i < max_common_size; ++i) {
    if (lhs[i]->get_reference_transition_id() < rhs[i]->get_reference_transition_id()) {
      return -1;
    }

    if (lhs[i]->get_reference_transition_id() > rhs[i]->get_reference_transition_id()) {
      return 1;
    }
  }

  if (lhs.size() == rhs.size()) {
    return 0;
  }
  // Different sized, the one with smallest length is smaller
  if (lhs.size() < rhs.size()) {
    return -1;
  }

  return 1;
}

/**
 * (From the paper)
 * For foata normal forms FC1 = C11 ... C1n1 and FC2 = C21 ... C2n2,
 * we say FC1 < FC2 if there is an i such that:
 *      C1j LEXICOGRAPHICALLY EQUAL to C2j for all j < i, and
 *      C1i LEXICOGRAPHICALLY SMALLER than C2i
 */
bool compare_foata_forms(const foata_normal_form_t& lhs, const foata_normal_form_t& rhs) {
  // We don't need to worry about having lhs.size() < rhs.size()
  // This method is only called when both local configurations are lexicographically equal
  // In this case, if lhs.size() > rhs.size()
  // the comparison will terminate somewhere before reaching rhs.size()
  legacy_embedded_debug_assert(lhs.size() <= rhs.size());

  for (size_t i{0}; i < lhs.size(); ++i) {
    const auto& lhs_local_conf_i{lhs[i]};
    const auto& rhs_local_conf_i{rhs[i]};

    const auto comp_result{lexicographically_compare(lhs_local_conf_i, rhs_local_conf_i)};
    if (comp_result != 0) {
      return (comp_result == -1);
    }
  }

  // Should reach here only if equal, but not the case in our code
  legacy_embedded_debug_assert(false);
  return false;
}

}  // namespace

bool total_adequate_order::operator()(unfolding_event& lhs, unfolding_event& rhs) const {
  const auto& lhs_local_conf{lhs.get_local_configuration()};
  const auto& rhs_local_conf{rhs.get_local_configuration()};

  if (lhs_local_conf.size() < rhs_local_conf.size()) {
    return true;
  }

  if (lhs_local_conf.size() == rhs_local_conf.size()) {
    const auto lex_compare{lexicographically_compare(lhs_local_conf, rhs_local_conf)};
    if (lex_compare == 0) {
      // Foata normal form compare
      const auto& lhs_foata{lhs.get_foata_normal_form()};
      const auto& rhs_foata{rhs.get_foata_normal_form()};
      return compare_foata_forms(lhs_foata, rhs_foata);
    }
    return (lex_compare == -1);
  }

  return false;
}

bool min_heap_total_adequate_order::operator()(unfolding_event* lhs, unfolding_event* rhs) const {
  // Well... This will return >= relationship, but AFAIK no two events are == so we are safe
  total_adequate_order order{};
  return !order(*lhs, *rhs);
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
