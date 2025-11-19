#pragma once

#include <random>

#include "column/vectorized_fwd.h"
#include "exprs/agg/nullable_aggregate.h"
#include "exprs/celonis/agg/explore_process.h"
#include "exprs/celonis/agg/variant_stats.h"
#include "google/protobuf/util/json_util.h"
#include "rapidjson/document.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"

namespace starrocks {

class ManagedAggrState {
public:
    ~ManagedAggrState();

    static std::unique_ptr<ManagedAggrState> create(FunctionContext* ctx, const AggregateFunction* func);

    AggDataPtr state();

private:
    ManagedAggrState(FunctionContext* ctx, const AggregateFunction* func);

    ManagedAggrState& operator=(const ManagedAggrState&) = delete;

    FunctionContext* _ctx;
    const AggregateFunction* _func;
    MemPool _mem_pool{};
    AggDataPtr _state;
};

struct ExploreProcessResult {
    std::vector<std::string> act_map; // idx -> activity_name
    std::vector<ActivityStats> a_stats;
    std::vector<size_t> a_stats_self_loop;
    std::vector<std::vector<std::pair<Variant, int>>> top;
    std::pair<Variant, int> happy;

    bool from_json(const std::string& json);
    bool equals(const ExploreProcessResult& other);
    bool equal_a_stats(const ExploreProcessResult& other, const std::vector<int>& remap_idx);
    bool equal_top(const ExploreProcessResult& other, const std::vector<int>& remap_idx);
    bool equal_happy(const ExploreProcessResult& other, const std::vector<int>& remap_idx);
    std::string debug_string() const;

private:
    bool try_parse_dict_from_json(const rapidjson::Document& document);
    bool try_parse_a_stats_from_json(const rapidjson::Document& document);
    Variant parse_variant_from_json(const rapidjson::Value& v);
    bool try_parse_top_from_json(const rapidjson::Document& document);
    bool try_parse_happy_from_json(const rapidjson::Document& document);
};

ArrayColumn::Ptr build_variant_column(const std::vector<std::vector<std::string>>& rows);

Column::Ptr build_weight_column(const std::vector<int>& weight);

ArrayColumn::Ptr build_random_variant_column(int seed_rows, int num_rows, int avg_length, unsigned int seed);

Column::Ptr build_random_weight_column(int num_rows, int avg_weight, unsigned int seed);

void match_result(const std::string& result1, const std::string& result2);

std::string compute_result_explore_process(const std::vector<std::pair<ArrayColumn::Ptr, Column::Ptr>>& data,
                                           int min_variant_count_threshold_on_leaf, bool enable_proto_encoding,
                                           FunctionContext* ctx);
// Used for explore_process_variant_stats_validation_test.cpp
std::string compute_result_variant_stats(const std::vector<std::pair<ArrayColumn::Ptr, Column::Ptr>>& data,
                                         FunctionContext* ctx);

std::optional<std::string> proto_to_statistics_json_string(const std::string& encoded_string);
} // namespace starrocks