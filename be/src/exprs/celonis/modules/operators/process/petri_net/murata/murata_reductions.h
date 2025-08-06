#pragma once

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_builder_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::murata {

/**
 * TODO (goulart.e) Implement the remaining rules.
 *
 * This method reduces by using a set of standard Petri net reductions (usually called "Murata" reductions).
 * The implementation of the rules is based on two resources:
 *
 * [1] T. Murata, "Petri nets: Properties, analysis and applications," in Proceedings of the IEEE, vol. 77, no. 4,
 *      pp. 541-580, April 1989, doi: 10.1109/5.24143. - FIGURE 22
 * [2] Verbeek, H.M.W. (2017). Decomposed Replay Using Hiding and Reduction as Abstraction. In: Koutny, M., Kleijn,
 *      J., Penczek, W. (eds) Transactions on Petri Nets and Other Models of Concurrency XII. Lecture Notes in Computer
 *      Science(), vol 10470. Springer, Berlin, Heidelberg. https://doi.org/10.1007/978-3-662-55862-1_8 - FIGURE 9
 *
 * The first resource actually presents reduction rules to preserve safeness, liveness and boundedness. The second
 * present rules to preserve the language. Notice that both resources are lousily specified, so each rule's exact
 * pre/post-conditions are documented in more detail in the docstring of their corresponding classes.
 *
 * The implementation differs in which the "visible" transitions are specified as an extra parameter (keep transitions).
 * This allows to also keep invisible labels if wanted (for example, if they represent a gateway). Notice that if a
 * visible transition is not part of "keep_transitions", then the reduction will not preserve the language anymore.
 * In any case, it will preserve liveness, safeness and boundedness.
 *
 * Each reduction removes at least one node. Finding the node to be reduced requires a pass over all nodes.
 * Checking if the pre-conditions of a net are met requires in the worst-case a pass over the net (but for real-world
 * nets this will actually be constant). So the total runtime is cubic (quadratic) in the size of the Petri net.
 *
 * @param pn_repr the petri_net_representation of the Petri net to be reduced. The Petri net is assumed to be valid.
 * @param keep_transitions the set of transition IDs to keep in the return set.
 * @return the reduced Petri net's representation.
 */
petri_net_representation reduce_murata(const petri_net_representation& pn_repr,
                                       const std::unordered_set<std::string>& keep_transitions);

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::murata
