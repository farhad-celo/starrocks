#include "cluster_strings.h"

#include <stack>
#include <chrono>

#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "runtime/mem_pool.h"

namespace starrocks {

namespace {

static const double MAX_CLUSTERING_SECONDS = 10 * 60.0;

static const uint8_t UTF8_BYTE_LENGTH_TABLE[256] = {
        // start byte of 1-byte utf8 char: 0b0000'0000 ~ 0b0111'1111
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        // continuation byte: 0b1000'0000 ~ 0b1011'1111
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        // start byte of 2-byte utf8 char: 0b1100'0000 ~ 0b1101'1111
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        // start byte of 3-byte utf8 char: 0b1110'0000 ~ 0b1110'1111
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        // start byte of 4-byte utf8 char: 0b1111'0000 ~ 0b1111'0111
        // invalid utf8 byte: 0b1111'1000~ 0b1111'1111
        4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1};

using Char = std::variant<char, std::string>;

std::vector<Char> to_chars(const std::string& s) {
    std::vector<Char> chars;
    chars.reserve(s.size());
    int char_size = 0;
    for (const char* str_p = s.data(), * str_end = str_p + s.size(); str_p < str_end; str_p += char_size) {
        char_size = UTF8_BYTE_LENGTH_TABLE[static_cast<uint8_t>(*str_p)];
        if (char_size == 1) {
            chars.emplace_back(*str_p);
        } else {
            chars.emplace_back(std::string(str_p, char_size));
        }
    }
    chars.shrink_to_fit();
    return chars;
}

uint64_t compute_alphanumeric_bitmask(const std::vector<Char>& chars) {
    uint64_t bitmask = 0;
    for (const auto& ch: chars) {
        std::visit([&bitmask](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, char>) {
                char c = arg;
                if (c >= '0' && c <= '9') {
                    bitmask |= 1ULL << (c - '0');
                } else if (c >= 'a' && c <= 'z') {
                    bitmask |= 1ULL << (c - 'a' + 10);
                } else if (c >= 'A' && c <= 'Z') {
                    bitmask |= 1ULL << (c - 'A' + 36);
                }
            }
        }, ch);
    }
    return bitmask;
}

struct String {
    std::vector<Char> chars = {};
    phmap::flat_hash_set<Char, StdHash<Char>> char_set = {};
    std::vector<bool> is_weighted_chars = {};
    // The weight for weighted chars, the weight for un-weighted char is 1.
    int64_t char_weight = 0;
    // number of chars which have positive weight (weight must be non-negative).
    int64_t real_len = 0;
    int64_t total_weight = 0;
    uint64_t alphanumeric_bitmask = 0;

    String(const std::string& s, const phmap::flat_hash_set<Char, StdHash<Char>>& weighted_chars, int64_t char_weight)
            : char_weight(char_weight) {
        chars = to_chars(s);
        real_len = 0;
        total_weight = 0;
        is_weighted_chars.reserve(chars.size());
        for (auto i = 0; i < chars.size(); ++i) {
            if (weighted_chars.find(chars[i]) != weighted_chars.end()) {
                is_weighted_chars.push_back(true);
                total_weight += char_weight;
                if (char_weight != 0) {
                    ++real_len;
                    char_set.insert(chars[i]);
                }
            } else {
                is_weighted_chars.push_back(false);
                char_set.insert(chars[i]);
                ++real_len;
                ++total_weight;
            }
        }
        alphanumeric_bitmask = compute_alphanumeric_bitmask(chars);
    }

    size_t size() const { return chars.size(); }

    size_t length() const { return chars.size(); }

    int64_t real_length() const { return real_len; }

    int64_t get_cost(size_t index) const { return is_weighted_chars[index] ? char_weight : 1; };

    const Char& operator[](size_t index) const {
        return chars[index];
    }
};

bool have_common_chars(const String& s1, const String& s2) {
    if ((s1.alphanumeric_bitmask & s2.alphanumeric_bitmask) != 0) {
        return true;
    }
    HashSet<Char> set1;
    for (auto i = 0; i < s1.size(); ++i) {
        set1.insert(s1[i]);
    }
    for (auto j = 0; j < s2.size(); ++j) {
        if (set1.find(s2[j]) != set1.end()) {
            return true;
        }
    }
    return false;
}

// Returns true if the weighted_edit_distance between s1 and s2 is not greater than threshold.
bool weighted_edit_distance_within_threshold(const String& s1, const String& s2, int64_t threshold) {
    if (std::abs(s1.real_length() - s2.real_length()) > threshold) {
        return false;
    }
    size_t m = s1.length();
    size_t n = s2.length();
    // dp[i][j] is the edit distance of s1[:i] and s2[:j].
    std::vector<std::vector<int64_t>> dp(m + 1, std::vector<int64_t>(n + 1, 0));
    for (auto i = 1; i < m + 1; ++i) {
        dp[i][0] = dp[i - 1][0] + s1.get_cost(i - 1);
    }
    for (auto j = 1; j < n + 1; ++j) {
        dp[0][j] = dp[0][j - 1] + s2.get_cost(j - 1);
    }
    for (auto i = 1; i < m + 1; ++i) {
        int64_t min_in_row = dp[i][0];
        for (auto j = 1; j < n + 1; ++j) {
            int64_t delete_cost = dp[i - 1][j] + s1.get_cost(i - 1);
            int64_t insert_cost = dp[i][j - 1] + s2.get_cost(j - 1);
            int64_t replace_cost = dp[i - 1][j - 1];
            if (s1[i - 1] != s2[j - 1]) {
                replace_cost += std::max(s1.get_cost(i - 1), s2.get_cost(j - 1));
            }
            dp[i][j] = std::min(replace_cost, std::min(delete_cost, insert_cost));
            min_in_row = std::min(dp[i][j], min_in_row);
        }
        // dp[m][n] >= min_in_row
        if (min_in_row > threshold) {
            return false;
        }
    }
    return dp[m][n] <= threshold;
}

std::vector<size_t>
get_cluster(const std::vector<std::vector<size_t>>& graph, size_t start, std::vector<bool>& visited) {
    std::vector<size_t> cluster;
    std::stack<size_t> nodes;
    nodes.push(start);
    visited[start] = true;
    while (!nodes.empty()) {
        size_t u = nodes.top();
        nodes.pop();
        cluster.push_back(u);
        for (auto v: graph[u]) {
            if (!visited[v]) {
                nodes.push(v);
                visited[v] = true;
            }
        }
    }
    return cluster;
}

struct StringClusterer {
    int64_t edit_threshold;
    phmap::flat_hash_set<Char, StdHash<Char>> weighted_chars;
    int64_t char_weight;

    StringClusterer(int64_t edit_threshold, const std::string& weighted_tokens, int64_t token_weight) : edit_threshold(
            edit_threshold), char_weight(token_weight) {
        std::vector<Char> tokens = to_chars(weighted_tokens);
        for (const auto& token: tokens) {
            weighted_chars.insert(token);
        }
    }

    std::vector<std::vector<int>>
    compute_char_sets(const std::vector<std::tuple<int128_t, String, std::string, int64_t>>& tuples) const {
        // compute char frequency
        phmap::flat_hash_map<Char, size_t, StdHash<Char>> char_counter;
        const auto n = tuples.size();
        for (auto i = 0; i < n; ++i) {
            const auto& s = std::get<1>(tuples[i]);
            for (const auto& ch: s.char_set) {
                ++char_counter[ch];
            }
        }
        std::vector<std::pair<Char, size_t>> char_cnt_pairs(char_counter.begin(), char_counter.end());
        // Sort chars based on frequency. Char with low frequency comes first.
        std::sort(char_cnt_pairs.begin(), char_cnt_pairs.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        phmap::flat_hash_map<Char, int, StdHash<Char>> char_to_index;
        for (int i = 0; i < char_cnt_pairs.size(); ++i) {
            char_to_index[char_cnt_pairs[i].first] = i;
        }
        std::vector<std::vector<int>> rv;
        rv.reserve(n);
        for (auto i = 0; i < n; ++i) {
            const auto& s = std::get<1>(tuples[i]);
            std::vector<int> chars;
            chars.reserve(s.char_set.size());
            for (const auto& ch: s.char_set) {
                auto it = char_to_index.find(ch);
                DCHECK(it != char_to_index.end());
                chars.push_back(it->second);
            }
            std::sort(chars.begin(), chars.end());
            rv.push_back(std::move(chars));
        }
        return rv;
    }

    phmap::flat_hash_map<int, std::vector<size_t>, StdHash<int>>
    build_prefix_index(const std::vector<std::vector<int>>& char_sets) const {
        phmap::flat_hash_map<int, std::vector<size_t>, StdHash<int>> char_to_indexes;
        // Given two strings (s1 and s2), if their set difference is d, their edit distance is at least ceil(d / 2).
        // Set n_tokens = 2 * edit_threshold + 2, if s1 and s2 do not have overlap in the first n_tokens chars in their
        // char set, their edit distance is at least edit_threshold + 1.
        const auto n_tokens = 2 * edit_threshold + 2;
        for (auto index = 0; index < char_sets.size(); ++index) {
            const auto& char_set = char_sets[index];
            for (auto j = 0; j < n_tokens && j < char_set.size(); ++j) {
                char_to_indexes[char_set[j]].push_back(index);
            }
        }
        LOG(INFO) << "CELONIS_CLUSTER_STRINGS: size of prefix_index is " << char_to_indexes.size() << std::endl;
        return char_to_indexes;
    }

    // Computes the neighbor indexes (< index) of the index-th string.
    std::vector<size_t> get_neighbors(const std::vector<std::vector<int>>& char_sets,
                                      const phmap::flat_hash_map<int, std::vector<size_t>, StdHash<int>>& char_to_indexes,
                                      size_t index) const {
        DCHECK(index >= 1);
        const auto& char_set = char_sets[index];
        const auto length = char_set.size();
        const auto epsilon = 2 * edit_threshold + 1;
        const auto max_length = length + epsilon;
        const auto min_length = (length >= epsilon) ? (length - epsilon) : 0;
        if (epsilon >= length) {
            std::vector<size_t> rv;
            rv.reserve(index);
            for (auto j = static_cast<int>(index) - 1; j >= 0; --j) {
                rv.push_back(j);
            }
            return rv;
        } else {
            HashSet<size_t> candidates = {};
            for (auto i = 0; i < epsilon + 1 && i < char_set.size(); ++i) {
                const auto& ch = char_set[i];
                auto it = char_to_indexes.find(ch);
                if (it == char_to_indexes.end()) {
                    continue;
                }
                for (auto j: it->second) {
                    // indexes is in ascending order.
                    if (j >= index) {
                        break;
                    }
                    if (char_sets[j].size() >= min_length && char_sets[j].size() <= max_length) {
                        candidates.insert(j);
                    }
                }
            }
            std::vector<size_t> rv(candidates.begin(), candidates.end());
            return rv;
        }
    }

    // TODO(y.zhang): Improve the efficiency.
    // Some ideas:
    // 1. For dense graph, using lazy exploration with union find approach is probably better.
    // 2. Use a bounded window when computing edit distance.
    // 2. Remove zero cost chars from chars in String.
    std::optional<std::vector<std::vector<size_t>>>
    build_graph(const std::vector<std::tuple<int128_t, String, std::string, int64_t>>& tuples) const {
        auto start_time = std::chrono::high_resolution_clock::now();
        const auto n = tuples.size();
        std::vector<std::vector<size_t>> graph(n, std::vector<size_t>(0));
        std::vector<std::vector<int>> char_sets = compute_char_sets(tuples);
        phmap::flat_hash_map<int, std::vector<size_t>, StdHash<int>> char_to_indexes = build_prefix_index(char_sets);
        for (auto i = 1; i < n; ++i) {
            auto length_i = std::get<1>(tuples[i]).real_length();
            auto neighbors = get_neighbors(char_sets, char_to_indexes, i);
            std::sort(neighbors.rbegin(), neighbors.rend());
            for (auto j: neighbors) {
                // edit_distance(s_i, s_j) >= abs(length_i - length_j), length_i >= length_j
                if (length_i - std::get<1>(tuples[j]).real_length() > edit_threshold) {
                    break;
                }
                // If two strings do not have any common chars, their edit distance is infinite (Same as what
                // Saola does).
                if (!have_common_chars(std::get<1>(tuples[i]), std::get<1>(tuples[j]))) {
                    continue;
                }
                // edit_distance(s_i, s_j) <= total_weight(s_i) + total_weight(s_j)
                if (edit_threshold >= std::get<1>(tuples[i]).total_weight + std::get<1>(tuples[j]).total_weight ||
                    weighted_edit_distance_within_threshold(std::get<1>(tuples[i]), std::get<1>(tuples[j]),
                                                            edit_threshold)) {
                    graph[i].push_back(j);
                    graph[j].push_back(i);
                }
            }
            if (i % 100 == 0) {
                auto cur_time = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> elapsed_time = cur_time - start_time;
                if (elapsed_time.count() > MAX_CLUSTERING_SECONDS) {
                    return std::nullopt;
                }
            }
        }
        return graph;
    }

    std::optional<std::vector<std::pair<int128_t, std::string>>>
    cluster(const phmap::flat_hash_map<int128_t, std::pair<std::string, int64_t>, StdHash<int128_t>>& hash_to_string_with_count) const {
        LOG(INFO) << "CELONIS_CLUSTER_STRINGS: started clustering\n";
        const auto n = hash_to_string_with_count.size();
        std::vector<std::tuple<int128_t, String, std::string, int64_t>> tuples;
        tuples.reserve(n);
        for (const auto& [hash128, string_with_count]: hash_to_string_with_count) {
            tuples.emplace_back(hash128, String(string_with_count.first, weighted_chars, char_weight),
                                string_with_count.first, string_with_count.second);
        }
        // strings are sorted based on their real length in ascending order.
        std::sort(tuples.begin(), tuples.end(),
                  [](const auto& a, const auto& b) {
                      return std::get<1>(a).real_length() < std::get<1>(b).real_length();
                  });
        // build the graph
        auto graph = build_graph(tuples);
        if (!graph.has_value()) {
            return std::nullopt;
        }
        LOG(INFO) << "CELONIS_CLUSTER_STRINGS: built graph\n";
        std::vector<bool> visited(n, false);
        std::vector<std::pair<int128_t, std::string>> rv;
        rv.reserve(n);
        for (auto i = 0; i < n; ++i) {
            if (!visited[i]) {
                std::vector<size_t> cluster = get_cluster(graph.value(), i, visited);
                // compute representative
                size_t representative = cluster[0];
                for (auto j = 0; j < cluster.size(); ++j) {
                    size_t idx = cluster[j];
                    if (std::get<3>(tuples[idx]) > std::get<3>(tuples[representative]) ||
                        (std::get<3>(tuples[idx]) == std::get<3>(tuples[representative]) &&
                         std::get<2>(tuples[idx]) < std::get<2>(tuples[representative]))) {
                        representative = idx;
                    }
                }
                // populate the cluster
                std::string rep_string = std::get<2>(tuples[representative]);
                for (auto idx: cluster) {
                    rv.emplace_back(std::get<0>(tuples[idx]), rep_string);
                }
            }
        }
        return rv;
    }
};

} // namespace

void ClusterStringsAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr state,
                                             size_t row_num) const {
    this->data(state).update(ctx, columns, row_num);
}

void ClusterStringsAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                            size_t row_num) const {
    // merge internal state with column[row_num]
    // the column type is binary
    const auto* input_column = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(column));
    if (input_column->is_null(row_num)) {
        return;
    }
    Slice slice = input_column->get_slice(row_num);
    this->data(state).deserialize_and_merge(ctx->mem_pool(), (const uint8_t*) slice.data);
}

void ClusterStringsAggregateFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                          Column* to) const {
    // append our serialized state to column "to"
    auto* column = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(to));
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    size_t old_size = column->get_bytes().size();
    size_t new_size = old_size + this->data(state).serialized_size();
    column->get_bytes().resize(new_size);
    this->data(state).serialize(column->get_bytes().data() + old_size);
    column->get_offset().emplace_back(new_size);
}

void ClusterStringsAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                  size_t chunk_size,
                                                                  ColumnPtr* dst) const {
    // Used for streaming aggregation. Not implemented.
    throw std::runtime_error("ClusterStrings: convert_to_serialize_format not supported");
}

void ClusterStringsAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                         Column* to) const {
    if (UNLIKELY(!ColumnHelper::get_data_column(to)->is_struct())) {
        ctx->set_error(std::string("The output column of " + get_name() +
                                   " finalize_to_column() is not struct, but is " + to->get_name())
                               .c_str(),
                       false);
        return;
    }
    auto& state_impl = this->data(state);
    const auto edit_threshold = state_impl.edit_threshold();
    const auto token_weight = state_impl.token_weight();
    const auto& weighted_tokens = state_impl.weighted_tokens();
    const auto& null_hashes = state_impl.null_hashes();
    const auto& hash_to_string_with_count = state_impl.hash_to_string_with_count();
    LOG(INFO) << "CELONIS_CLUSTER_STRINGS: number of unique strings is " << hash_to_string_with_count.size()
              << std::endl;
    StringClusterer clusterer(edit_threshold, weighted_tokens, token_weight);
    auto hash_string_pairs = clusterer.cluster(hash_to_string_with_count);
    if (!hash_string_pairs.has_value()) {
        ctx->set_error(std::string("CELONIS_CLUSTER_STRINGS timeout").c_str(), false);
        return;
    }
    // write to output column
    LOG(INFO) << "CELONIS_CLUSTER_STRINGS: writing to column\n";
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    if (fields[0]->is_nullable()) {
        down_cast<NullableColumn*>(fields[0].get())->null_column_data().emplace_back(0);
    }
    if (fields[1]->is_nullable()) {
        down_cast<NullableColumn*>(fields[1].get())->null_column_data().emplace_back(0);
    }
    auto hash_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    auto representative_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    uint32_t n_elements = 0;
    for (const auto& [hash128, str]: hash_string_pairs.value()) {
        hash_col->elements_column()->append_datum(hash128);
        representative_col->elements_column()->append_datum(Slice(str));
        ++n_elements;
    }
    for (int128_t hash128: null_hashes) {
        hash_col->elements_column()->append_datum(hash128);
        representative_col->elements_column()->append_datum(kNullDatum);
        ++n_elements;
    }
    auto& hash_offsets = hash_col->offsets_column()->get_data();
    hash_offsets.push_back(hash_offsets.back() + n_elements);
    auto& representative_offsets = representative_col->offsets_column()->get_data();
    representative_offsets.push_back(representative_offsets.back() + n_elements);
}

std::string ClusterStringsAggregateFunction::get_name() const { return "celonis_cluster_strings"; }

} // namespace starrocks
