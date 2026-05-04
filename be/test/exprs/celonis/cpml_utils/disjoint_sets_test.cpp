#include "exprs/celonis/cpml_utils/disjoint_sets.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <atomic>
#include <random>
#include <ranges>
#include <thread>
#include <unordered_map>

namespace starrocks::celonis::cpml_utils {

namespace {

/**
 * Given a set of elements, it creates a hashmap such that every element
 * has a unique index between 0 ... N-1, where N is the number of elements in
 * the Container. It should be noted that the lifetime of the elements should
 * exceed the lifetime of the created index.
 */
template <typename CONTAINER, typename SET_ID>
auto indexing_creator(const CONTAINER& elements) {
    using DATATYPE = CONTAINER::value_type;
    std::unordered_map<const DATATYPE*, SET_ID> indexing;

    SET_ID current_index{0};

    for (auto& element : elements) {
        indexing.insert({&element, current_index});
        ++current_index;
    }

    return indexing;
}

using SEQUENTIAL_DSU_ID = std::size_t;
using THREADED_DSU_ID = std::size_t;

/** A sequential implementation of disjoint set data structure. The user should manage the ELEMENT->ID
 * indexes by themselves. For help the indexing_creator() function may be used.
 * I assume that there exist an ELEMENT->THREADED_DSU_ID mapping such that these are the
 * numbers from 0 to N-1. The disjoint_set data structure can be used for undirected graph connectivity problems.
 */
class disjoint_set {
public:
    explicit disjoint_set(const SEQUENTIAL_DSU_ID number_of_elements);

    [[nodiscard]] SEQUENTIAL_DSU_ID find_set(SEQUENTIAL_DSU_ID element);

    void union_set(SEQUENTIAL_DSU_ID first, SEQUENTIAL_DSU_ID second);

    [[nodiscard]] SEQUENTIAL_DSU_ID count_sets() const;

    [[nodiscard]] SEQUENTIAL_DSU_ID get_parents_size() const;

private:
    std::vector<SEQUENTIAL_DSU_ID> parents_;
    std::vector<SEQUENTIAL_DSU_ID> ranks_;
};

inline disjoint_set::disjoint_set(const std::size_t number_of_elements) {
    parents_.resize(number_of_elements);
    std::iota(parents_.begin(), parents_.end(), SEQUENTIAL_DSU_ID{0});
    ranks_.resize(number_of_elements);
}

/**
 * Given the index of ELEMENT, this function finds the root of the tree the ELEMENT is in.
 */
inline SEQUENTIAL_DSU_ID disjoint_set::find_set(const SEQUENTIAL_DSU_ID element) {
    if (element >= SEQUENTIAL_DSU_ID{parents_.size()}) {
        throw std::runtime_error("Element does not exist.");
    }
    auto root{element};

    while (parents_[root] != root) {
        root = parents_[root];
    }

    auto curr_element{element};
    while (parents_[curr_element] != root) {
        curr_element = std::exchange(parents_[curr_element], root);
    }

    return root;
}

/**
 * Given the indexes of two ELEMENTS it unifies the two trees these two belong to.
 */
inline void disjoint_set::union_set(const SEQUENTIAL_DSU_ID first, const SEQUENTIAL_DSU_ID second) {
    if (first >= SEQUENTIAL_DSU_ID{parents_.size()} || second >= SEQUENTIAL_DSU_ID{parents_.size()}) {
        throw std::runtime_error("Element does not exist.");
    }
    auto first_root{find_set(first)};
    auto second_root{find_set(second)};
    if (ranks_[first_root] > ranks_[second_root]) {
        parents_[second_root] = first_root;
    } else {
        parents_[first_root] = second_root;
        if (ranks_[second_root] == ranks_[first_root]) {
            ++ranks_[second_root];
        }
    }
}

/** Counts the number of trees in the graph.*/
inline SEQUENTIAL_DSU_ID disjoint_set::count_sets() const {
    std::size_t count = 0;
    for (const auto& element : std::views::iota(SEQUENTIAL_DSU_ID{0}, SEQUENTIAL_DSU_ID{parents_.size()})) {
        if (element == parents_[element]) {
            ++count;
        }
    }
    return count;
}

inline SEQUENTIAL_DSU_ID disjoint_set::get_parents_size() const {
    return parents_.size();
}

[[nodiscard]] std::pair<disjoint_set, std::vector<std::pair<size_t, size_t>>> random_graph(
        const size_t number_of_nodes, const size_t number_of_edges, const std::optional<int> seed = std::nullopt) {
    disjoint_set disjoint_sets{number_of_nodes};

    std::vector<std::pair<size_t, size_t>> edges(number_of_edges);

    std::mt19937 gen(seed.value_or(std::random_device{}()));
    std::uniform_int_distribution distrib(size_t{0}, number_of_nodes - 1);

    for (size_t i = 0; i < number_of_edges; ++i) {
        size_t u = distrib(gen);
        size_t v = distrib(gen);
        disjoint_sets.union_set(u, v);
        edges[i] = std::make_pair(u, v);
    }

    return {std::move(disjoint_sets), std::move(edges)};
}

std::vector<std::vector<SEQUENTIAL_DSU_ID>> connected_components(disjoint_set& ds) {
    const auto parents_size = ds.get_parents_size();
    std::unordered_map<THREADED_DSU_ID, std::vector<THREADED_DSU_ID>> raw_groups;
    for (const auto& element : std::views::iota(THREADED_DSU_ID{0}, THREADED_DSU_ID{parents_size})) {
        raw_groups[ds.find_set(element)].push_back(element);
    }

    std::vector<std::vector<THREADED_DSU_ID>> connected_components;
    connected_components.reserve(raw_groups.size());
    for (auto& val : raw_groups | std::views::values) {
        connected_components.push_back(std::move(val));
    }

    std::ranges::sort(connected_components);

    return connected_components;
}

std::vector<std::vector<THREADED_DSU_ID>> connected_components(
        const starrocks::celonis::cpml_utils::details::threaded_disjoint_set& threaded_ds) {
    const auto parents_size = threaded_ds.get_parents_size();
    std::unordered_map<THREADED_DSU_ID, std::vector<THREADED_DSU_ID>> raw_groups;
    for (const auto& element : std::views::iota(THREADED_DSU_ID{0}, THREADED_DSU_ID{parents_size})) {
        raw_groups[threaded_ds.find_set(element)].push_back(element);
    }

    std::vector<std::vector<THREADED_DSU_ID>> connected_components;
    connected_components.reserve(raw_groups.size());
    for (auto& val : raw_groups | std::views::values) {
        connected_components.push_back(std::move(val));
    }

    std::ranges::sort(connected_components);

    return connected_components;
}

using lhs_partition_size_t = disjoint_sets::lhs_partition_size_t;
using rhs_partition_size_t = disjoint_sets::rhs_partition_size_t;
using lhs_index_t = disjoint_sets::lhs_index_t;
using rhs_index_t = disjoint_sets::rhs_index_t;

} // namespace

TEST(CelonisDisjointSetTest, IndexCreation) {
    std::vector<std::string> heavy_objects;
    constexpr size_t number_of_elements{1000};
    constexpr size_t max_length_of_strings{10000};
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> distrib_length(0, max_length_of_strings);
    std::uniform_int_distribution<int> distrib_char(33, 126);
    for (size_t i = 0; i < number_of_elements; ++i) {
        const int string_length = distrib_length(gen);
        std::string heavy_object{};
        for (int j = 0; j < string_length; ++j) {
            heavy_object.push_back(static_cast<char>(distrib_char(gen)));
        }
        heavy_objects.push_back(std::move(heavy_object));
    }

    const std::unordered_map<const std::string*, SEQUENTIAL_DSU_ID> indexes =
            indexing_creator<std::vector<std::string>, SEQUENTIAL_DSU_ID>(heavy_objects);

    auto range_of_ids = std::views::iota(size_t{0}, number_of_elements);
    std::set<size_t> expected{range_of_ids.begin(), range_of_ids.end()};

    std::set<size_t> seen_ids{};

    for (const auto& val : indexes | std::views::values) {
        seen_ids.insert(val);
    }

    EXPECT_TRUE(seen_ids == expected);
}

TEST(CelonisDisjointSetTest, BoostTest) {
    constexpr std::size_t number_of_elements{4};

    disjoint_set ds{number_of_elements};

    EXPECT_TRUE(ds.find_set(0) != ds.find_set(1));
    EXPECT_TRUE(ds.find_set(0) != ds.find_set(2));
    EXPECT_TRUE(ds.find_set(0) != ds.find_set(3));
    EXPECT_TRUE(ds.find_set(1) != ds.find_set(2));
    EXPECT_TRUE(ds.find_set(1) != ds.find_set(3));
    EXPECT_TRUE(ds.find_set(2) != ds.find_set(3));

    EXPECT_TRUE(ds.find_set(0) == ds.find_set(0));
    EXPECT_TRUE(ds.find_set(1) == ds.find_set(1));
    EXPECT_TRUE(ds.find_set(2) == ds.find_set(2));
    EXPECT_TRUE(ds.find_set(3) == ds.find_set(3));

    ds.union_set(0, 1);
    ds.union_set(2, 3);
    EXPECT_TRUE(ds.find_set(0) != ds.find_set(3));
    size_t a = ds.find_set(size_t{0});
    EXPECT_TRUE(a == ds.find_set(1));
    size_t b = ds.find_set(size_t{2});
    EXPECT_TRUE(b == ds.find_set(3));

    EXPECT_TRUE(ds.count_sets() == 2);
    const auto expected_components{
            std::vector<std::vector<size_t>>{std::vector<size_t>{0, 1}, std::vector<size_t>{2, 3}}};
    EXPECT_TRUE(connected_components(ds) == expected_components);
}

TEST(CelonisDisjointSetTest, ReferenceTest) {
    constexpr std::size_t number_of_elements{4};
    const std::vector<std::string> text{"ma", "holnap", "tegnap", "soha"};
    std::unordered_map<const std::string*, SEQUENTIAL_DSU_ID> ids =
            indexing_creator<std::vector<std::string>, SEQUENTIAL_DSU_ID>(text);
    disjoint_set ds{number_of_elements};

    const auto* string_ma = &text[0];
    const auto* string_holnap = &text[1];
    const auto* string_tegnap = &text[2];
    const auto* string_soha = &text[3];

    EXPECT_TRUE(ds.find_set(ids[string_ma]) != ds.find_set(ids[string_tegnap]));
    EXPECT_TRUE(ds.find_set(ids[string_soha]) != ds.find_set(ids[string_holnap]));
    EXPECT_TRUE(ds.find_set(ids[string_soha]) != ds.find_set(ids[string_tegnap]));

    EXPECT_TRUE(ds.find_set(ids[string_ma]) == ds.find_set(ids[string_ma]));
    EXPECT_TRUE(ds.find_set(ids[string_soha]) == ds.find_set(ids[string_soha]));
    EXPECT_TRUE(ds.find_set(ids[string_tegnap]) == ds.find_set(ids[string_tegnap]));
}

TEST(CelonisDisjointSetTest, Threaded_DSU_SequentialTest) {
    constexpr size_t number_of_elements{4};
    const std::vector<size_t> elements{0, 1, 2, 3};
    starrocks::celonis::cpml_utils::details::threaded_disjoint_set threaded_ds(number_of_elements);

    for (const auto& element : elements) {
        EXPECT_TRUE(threaded_ds.find_set(element) == element);
    }

    threaded_ds.union_set(0, 1);
    EXPECT_TRUE(threaded_ds.find_set(0) == threaded_ds.find_set(1));
    EXPECT_TRUE(threaded_ds.find_set(0) != threaded_ds.find_set(2));
    threaded_ds.union_set(0, 1);
    threaded_ds.union_set(1, 2);

    EXPECT_TRUE(threaded_ds.find_set(0) == threaded_ds.find_set(1));
    EXPECT_TRUE(threaded_ds.find_set(0) == threaded_ds.find_set(2));
}

TEST(CelonisDisjointSetTest, ThreadedDSUSequentialBoostTest) {
    constexpr std::size_t number_of_elements{4};

    starrocks::celonis::cpml_utils::details::threaded_disjoint_set threaded_ds{number_of_elements};

    EXPECT_TRUE(threaded_ds.find_set(0) != threaded_ds.find_set(1));
    EXPECT_TRUE(threaded_ds.find_set(0) != threaded_ds.find_set(2));
    EXPECT_TRUE(threaded_ds.find_set(0) != threaded_ds.find_set(3));
    EXPECT_TRUE(threaded_ds.find_set(1) != threaded_ds.find_set(2));
    EXPECT_TRUE(threaded_ds.find_set(1) != threaded_ds.find_set(3));
    EXPECT_TRUE(threaded_ds.find_set(2) != threaded_ds.find_set(3));

    threaded_ds.union_set(0, 1);
    threaded_ds.union_set(2, 3);
    EXPECT_TRUE(threaded_ds.find_set(0) != threaded_ds.find_set(3));
    size_t a = threaded_ds.find_set(0);
    EXPECT_TRUE(a == threaded_ds.find_set(1));
    size_t b = threaded_ds.find_set(2);
    EXPECT_TRUE(b == threaded_ds.find_set(3));
    EXPECT_TRUE(threaded_ds.count_sets() == 2);
    EXPECT_TRUE(threaded_ds.count_sets(std::execution::par) == 2);
    EXPECT_TRUE(threaded_ds.count_sets(std::execution::par_unseq) == 2);
}

TEST(CelonisDisjointSetTest, ThreadedDSUDisjointlyParallelTest) {
    constexpr std::size_t number_of_elements{1000};
    constexpr int number_of_threads{10};
    details::threaded_disjoint_set threaded_ds{number_of_elements};

    std::vector<std::jthread> threads;

    for (size_t i{0}; i < number_of_threads; ++i) {
        threads.emplace_back([&threaded_ds, i]() {
            const size_t chunk{number_of_elements / number_of_threads};
            const size_t start{i * chunk};
            const size_t end{start + chunk};

            for (size_t j{start}; j < end; j += 2) {
                threaded_ds.union_set(j, j + 1);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    threads.clear();

    std::atomic<uint32_t> num_successes{0};
    for (size_t i{0}; i < number_of_threads; ++i) {
        threads.emplace_back([&threaded_ds, &num_successes, i]() {
            const size_t chunk{number_of_elements / number_of_threads};
            const size_t start{i * chunk};
            const size_t end{start + chunk};

            for (size_t j{start}; j < end; j += 2) {
                num_successes.fetch_add(static_cast<uint32_t>(threaded_ds.find_set(j) == threaded_ds.find_set(j + 1)),
                                        std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(num_successes.load() == number_of_elements / 2);
}

TEST(CelonisDisjointSetTest, ThreadedDSUConnectedlyParallelTest) {
    constexpr std::size_t number_of_elements{1000};
    constexpr int number_of_threads{10};

    auto [sequential_ds, edges] = random_graph(number_of_elements, number_of_elements);
    const size_t number_of_edges{edges.size()};

    details::threaded_disjoint_set threaded_ds{number_of_elements};

    std::vector<std::jthread> threads;

    for (int i = 0; i < number_of_threads; ++i) {
        threads.emplace_back([&threaded_ds, &edges, &number_of_edges, i]() {
            const int chunk = static_cast<int>(number_of_edges) / number_of_threads;
            const int start = i * chunk;
            const int end = start + chunk;

            for (int j = start; j < end; ++j) {
                threaded_ds.union_set(edges[j].first, edges[j].second);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(connected_components(threaded_ds) == connected_components(sequential_ds));
}

TEST(CelonisDisjointSetTest, BoostTestForStrongTypedDSU) {
    constexpr lhs_partition_size_t number_of_elements_lhs{4};
    constexpr rhs_partition_size_t number_of_elements_rhs{4};

    disjoint_sets ds{disjoint_sets::create_for_pushout(number_of_elements_lhs, number_of_elements_rhs)};

    for (std::size_t idx1{0}; idx1 < 3; ++idx1) {
        for (std::size_t idx2{idx1 + 1}; idx2 <= 3; ++idx2) {
            EXPECT_TRUE(ds.find_set(lhs_index_t{idx1}) != ds.find_set(lhs_index_t{idx2}));
            EXPECT_TRUE(ds.find_set(lhs_index_t{idx1}) != ds.find_set(rhs_index_t{idx2}));
            EXPECT_TRUE(ds.find_set(rhs_index_t{idx1}) != ds.find_set(rhs_index_t{idx2}));
        }
    }

    for (std::size_t idx{0}; idx <= 3; ++idx) {
        EXPECT_TRUE(ds.find_set(lhs_index_t{idx}) == ds.find_set(lhs_index_t{idx}));
        EXPECT_TRUE(ds.find_set(rhs_index_t{idx}) == ds.find_set(rhs_index_t{idx}));
    }

    ds.union_set(lhs_index_t{0}, rhs_index_t{1});
    ds.union_set(lhs_index_t{2}, rhs_index_t{3});
    EXPECT_TRUE(ds.find_set(lhs_index_t{0}) != ds.find_set(lhs_index_t{3}));
    size_t a = ds.find_set(lhs_index_t{0});
    EXPECT_TRUE(a == ds.find_set(rhs_index_t{1}));
    size_t b = ds.find_set(lhs_index_t{2});
    EXPECT_TRUE(b == ds.find_set(rhs_index_t{3}));
}

} // namespace starrocks::celonis::cpml_utils