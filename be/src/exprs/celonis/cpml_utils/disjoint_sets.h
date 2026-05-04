#pragma once

#include <ctl/named_type.h>
#include <fmt/format.h>

#include <atomic>
#include <execution>
#include <iostream>
#include <ranges>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace starrocks::celonis::cpml_utils {
namespace details {
using THREADED_DSU_ID = std::size_t;

/**
 * Implementation from: https://ldhulipala.github.io/papers/connectit-full.pdf
 * I assume that there exist an ELEMENT->THREADED_DSU_ID mapping such that these are the
 * numbers from 0 to N-1. One can use for example the indexing_creator() function for this purpose.
 * The union and find operations should be done in batches, if you want
 * sequential consistency as the algorithm only satisfies linearization.
 */
class threaded_disjoint_set {
public:
    explicit threaded_disjoint_set(const THREADED_DSU_ID number_of_elements);
    explicit threaded_disjoint_set(std::vector<THREADED_DSU_ID>&& parents) : parents_{std::move(parents)} {};
    [[nodiscard]] THREADED_DSU_ID find_set(const THREADED_DSU_ID element) const;

    bool union_set(const THREADED_DSU_ID first, const THREADED_DSU_ID second);

    template <typename ExecMode = std::execution::sequenced_policy>
    [[nodiscard]] THREADED_DSU_ID count_sets(ExecMode policy = std::execution::seq) const;

    [[nodiscard]] THREADED_DSU_ID get_parents_size() const;

private:
    void make_set(const THREADED_DSU_ID element);

    std::vector<THREADED_DSU_ID> parents_;
};

inline threaded_disjoint_set::threaded_disjoint_set(const THREADED_DSU_ID number_of_elements) {
    parents_.resize(number_of_elements);
    for (const auto& element : std::views::iota(size_t{0}, number_of_elements)) {
        make_set(element);
    }
}

/**
 * Implementing uncompressing find. Given the index of ELEMENT, this function finds the root of the tree the ELEMENT is
 * in.
 */
inline THREADED_DSU_ID threaded_disjoint_set::find_set(const THREADED_DSU_ID element) const {
    if (element >= THREADED_DSU_ID{parents_.size()}) {
        throw std::invalid_argument(fmt::format("The element id should be in the interval of [0, {} - 1]",
                                                THREADED_DSU_ID{parents_.size()}));
    }
    THREADED_DSU_ID current_element{element};
    while (current_element != parents_[current_element]) {
        current_element = parents_[current_element];
    }
    return current_element;
}

/**
 * Implementing the REM-CAS version. Given the indexes of two ELEMENTS it unifies the two trees these two belong.
 * Returns true if the two ELEMENTS were in different components and merged it. Returns false if the two ELEMENTS are
 * already in one component.
 */
inline bool threaded_disjoint_set::union_set(const THREADED_DSU_ID first, const THREADED_DSU_ID second) {
    if (first >= THREADED_DSU_ID{parents_.size()} || second >= THREADED_DSU_ID{parents_.size()}) {
        throw std::invalid_argument(
                fmt::format("The set id should be in the interval of [0, {} - 1]", THREADED_DSU_ID{parents_.size()}));
    }
    THREADED_DSU_ID current_first{first};
    THREADED_DSU_ID current_second{second};
    for (auto parent_first = std::atomic_ref<THREADED_DSU_ID>{parents_[current_first]}.load(std::memory_order_acquire),
              parent_second =
                      std::atomic_ref<THREADED_DSU_ID>{parents_[current_second]}.load(std::memory_order_acquire);
         parent_first != parent_second;) {
        if (parent_first > parent_second) {
            std::swap(current_first, current_second);
            std::swap(parent_first, parent_second);
        }
        if (current_first == parent_first &&
            std::atomic_ref<THREADED_DSU_ID>(parents_[current_first])
                    .compare_exchange_strong(parent_first, parent_second, std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
            return true;
        }

        if (const auto grand_parent_first{
                    std::atomic_ref<THREADED_DSU_ID>{parents_[parent_first]}.load(std::memory_order_acquire)};
            std::atomic_ref<THREADED_DSU_ID>{parents_[current_first]}.compare_exchange_weak(
                    parent_first, grand_parent_first, std::memory_order_acq_rel, std::memory_order_acquire)) {
            current_first = parent_first;
            parent_first = grand_parent_first;
        }
    }
    return false;
}

/** Counts the number of trees in the graph.*/
template <typename ExecMode>
inline THREADED_DSU_ID threaded_disjoint_set::count_sets(ExecMode policy) const {
    auto indices = std::views::iota(THREADED_DSU_ID{0}, THREADED_DSU_ID{parents_.size()});
    const THREADED_DSU_ID count =
            std::transform_reduce(policy, indices.begin(), indices.end(), THREADED_DSU_ID{0}, std::plus<>(),
                                  [&](const THREADED_DSU_ID i) { return parents_[i] == i ? 1 : 0; });

    return count;
}

inline THREADED_DSU_ID threaded_disjoint_set::get_parents_size() const {
    return parents_.size();
}

inline void threaded_disjoint_set::make_set(const THREADED_DSU_ID element) {
    parents_[element] = element;
}
} // namespace details

using set_id_t = details::THREADED_DSU_ID;
using row_id = details::THREADED_DSU_ID;

/**
 * This class has two concerns:
 *   1) Initializing and taking ownership of the threaded_disjoint_set data structure
 *   2) Map indices in the input partitions into indices into the disjoint_sets datastructure
 *
 * Regarding (2), this is necessary as the disjoint_sets datastructure is not aware of the coequalizer/pushout
 * partitions. Instead, it works with sets. Each instance in a target partition represents a set.
 *
 * For the coequalizer case we have a single target partition an index into the
 * target partition can be directly mapped to the correspoinding set ID. However, for the pushout case we have two
 * target partitions and we "stack" their associated set IDs in the disjoint set datastructure.
 *
 *   Sets formed by instances in the COEQUALIZER target partition:
 *   [<---- target partition instances ---->]
 *
 *   Sets formed by instances in the PUSHOUT target partitions:
 *   [<---- lhs target partition instances ---->|<---- rhs target partition instances ---->]
 */
class disjoint_sets {
public:
    using lhs_partition_size_t = ctl::named_type<row_id, struct lhs_partition_size_tag>;
    using rhs_partition_size_t = ctl::named_type<row_id, struct rhs_partition_size_tag>;
    using parent_vector_t = std::vector<set_id_t>;
    [[nodiscard]] static disjoint_sets create_for_pushout(lhs_partition_size_t lhs_partition_size,
                                                          rhs_partition_size_t rhs_partition_size);

    [[nodiscard]] static disjoint_sets create_for_coequalizer(row_id target_partition_size);
    disjoint_sets(row_id rhs_set_id_offset, parent_vector_t&& parent_vector)
            : rhs_set_id_offset_{rhs_set_id_offset},
              threaded_disjoint_sets_(details::threaded_disjoint_set(std::move(parent_vector))) {}
    using lhs_index_t = ctl::named_type<row_id, struct lhs_index_tag, ctl::comparable>;
    using rhs_index_t = ctl::named_type<row_id, struct rhs_index_tag, ctl::comparable>;

    inline void union_set(lhs_index_t lhs_idx, rhs_index_t rhs_idx) {
        const auto lhs_set_id{lhs_idx.get()};
        const auto rhs_set_id{rhs_set_id_offset_ + rhs_idx.get()};
        threaded_disjoint_sets_.union_set(lhs_set_id, rhs_set_id);
    };

    [[nodiscard]] set_id_t find_set(lhs_index_t lhs_idx) const;
    [[nodiscard]] set_id_t find_set(rhs_index_t rhs_idx) const;
    [[nodiscard]] set_id_t count_sets() const;

private:
    disjoint_sets(row_id number_of_sets, row_id rhs_set_id_offset);

    row_id rhs_set_id_offset_{};
    details::threaded_disjoint_set threaded_disjoint_sets_;
};

inline disjoint_sets disjoint_sets::create_for_pushout(const lhs_partition_size_t lhs_partition_size,
                                                       const rhs_partition_size_t rhs_partition_size) {
    if (std::numeric_limits<set_id_t>::max() - lhs_partition_size.get() < rhs_partition_size.get()) {
        throw std::invalid_argument(
                fmt::format("Instances in the LHS- and RHS partitions (sizes [{}] and [{}] respectively) cannot be "
                            "uniquely assigned an ID of type set_id_t (numeric limits: min {}, max {}).",
                            lhs_partition_size.get(), rhs_partition_size.get(), std::numeric_limits<set_id_t>::min(),
                            std::numeric_limits<set_id_t>::max()));
    }
    const auto number_of_sets{lhs_partition_size.get() + rhs_partition_size.get()};
    const auto rhs_set_id_offset{lhs_partition_size.get()};

    return disjoint_sets{number_of_sets, rhs_set_id_offset};
}

inline disjoint_sets disjoint_sets::create_for_coequalizer(row_id target_partition_size) {
    const auto number_of_sets{target_partition_size};
    const row_id rhs_set_id_offset{0};

    return disjoint_sets{number_of_sets, rhs_set_id_offset};
}

inline set_id_t disjoint_sets::find_set(const lhs_index_t lhs_idx) const {
    const auto lhs_set_id{lhs_idx.get()};
    return static_cast<set_id_t>(threaded_disjoint_sets_.find_set(lhs_set_id));
}

inline set_id_t disjoint_sets::find_set(const rhs_index_t rhs_idx) const {
    const auto rhs_set_id{rhs_set_id_offset_ + rhs_idx.get()};
    return static_cast<set_id_t>(threaded_disjoint_sets_.find_set(rhs_set_id));
}

inline disjoint_sets::disjoint_sets(const row_id number_of_sets, const row_id rhs_set_id_offset)
        : rhs_set_id_offset_{rhs_set_id_offset}, threaded_disjoint_sets_{static_cast<std::size_t>(number_of_sets)} {}
} // namespace starrocks::celonis::cpml_utils