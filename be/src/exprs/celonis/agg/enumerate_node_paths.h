#pragma once

#include <fmt/format.h>

#include "column/datum.h"
#include "column/hash_set.h"
#include "column/type_traits.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "gutil/casts.h"
#include "runtime/runtime_state.h"
#include "types/logical_type_infra.h"

namespace starrocks {

class Enumerator;

struct ColumnsKey {
    size_t hash{0};
    struct CommonInfo {
        const Column** columns;
        LogicalType* types;
        int32_t num_columns;
    };
    CommonInfo* info;
    int32_t offset;

    ColumnsKey() = default;

    // If o is -1, it constructs a key with all NULLs.
    ColumnsKey(CommonInfo* ci, int32_t o) : info(ci), offset(o) {
        if (has_any_null()) {
            // Special key with any NULL
            for (int i = 0; i < info->num_columns; ++i) {
                HashUtil::hash_combine(hash, 0);
            }
            return;
        }
        bool any_null = false;
        for (int i = 0; i < info->num_columns; ++i) {
            if (info->columns[i]->is_null(offset)) {
                HashUtil::hash_combine(hash, 0);
                any_null = true;
                continue;
            }
            const auto type = info->types[i];
            switch (type) {
            case TYPE_VARCHAR:
                HashUtil::hash_combine(hash, SliceHash()(info->columns[i]->get(offset).get_slice()));
                break;
#define M(type)                                                                                                    \
    case type:                                                                                                     \
        HashUtil::hash_combine(                                                                                    \
                hash, StdHash<RunTimeCppType<type>>()(info->columns[i]->get(offset).get<RunTimeCppType<type>>())); \
        break;

                APPLY_FOR_ALL_NUMBER_TYPE(M)
                M(TYPE_DATETIME)
#undef M
            default:
                throw std::runtime_error(fmt::format("Type {} not supported", type));
            }
        }
        if (any_null) {
            offset = -1;
            hash = 0;
            for (int i = 0; i < info->num_columns; ++i) {
                HashUtil::hash_combine(hash, 0);
            }
        }
    }

    Datum get(int idx) const {
        if (has_any_null()) {
            return kNullDatum;
        }
        return info->columns[idx]->get(offset);
    }

    bool has_any_null() const {
        return offset == -1;
    }
};

struct EqualOnColumnsKey {
    bool operator()(const ColumnsKey& x, const ColumnsKey& y) const {
        DCHECK_EQ(x.info->num_columns, y.info->num_columns);
        if (x.hash != y.hash) return false;
        if (x.has_any_null()) {
            return y.has_any_null();
        } else if (y.has_any_null()) {
            return false;
        } else {
            for (int i = 0; i < x.info->num_columns; ++i) {
                if (x.info->columns[i]->equals(x.offset, *y.info->columns[i], y.offset) != Column::EQUALS_TRUE) {
                    return false;
                }
            }
        }
        return true;
    }
};

struct HashOnColumnsKey {
    std::size_t operator()(const ColumnsKey& x) const { return x.hash; }
};

struct DedupColumnsKey {
    size_t hash{0};
    ColumnsKey::CommonInfo* info;
    int32_t offset;

    DedupColumnsKey() = default;

    // If o is -1, it constructs a key with all NULLs.
    DedupColumnsKey(ColumnsKey::CommonInfo* ci, int32_t o) : info(ci), offset(o) {
        if (is_all_nulls()) {
            // Special key with all NULLs
            for (int i = 0; i < info->num_columns; ++i) {
                HashUtil::hash_combine(hash, 0);
            }
            return;
        }
        bool all_nulls = true;
        for (int i = 0; i < info->num_columns; ++i) {
            if (info->columns[i]->is_null(offset)) {
                HashUtil::hash_combine(hash, 0);
                continue;
            }
            all_nulls = false;
            const auto type = info->types[i];
            switch (type) {
            case TYPE_VARCHAR:
                HashUtil::hash_combine(hash, SliceHash()(info->columns[i]->get(offset).get_slice()));
                break;
#define M(type)                                                                                                    \
    case type:                                                                                                     \
        HashUtil::hash_combine(                                                                                    \
                hash, StdHash<RunTimeCppType<type>>()(info->columns[i]->get(offset).get<RunTimeCppType<type>>())); \
        break;

                APPLY_FOR_ALL_NUMBER_TYPE(M)
                M(TYPE_DATETIME)
#undef M
            default:
                throw std::runtime_error(fmt::format("Type {} not supported", type));
            }
        }
        if (all_nulls) {
            offset = -1;
        }
    }

    Datum get(int idx) const {
        if (is_all_nulls()) {
            return kNullDatum;
        }
        return info->columns[idx]->get(offset);
    }

    bool is_all_nulls() const {
        return offset == -1;
    }
};

struct EqualOnDedupColumnsKey {
    bool operator()(const DedupColumnsKey& x, const DedupColumnsKey& y) const {
        DCHECK_EQ(x.info->num_columns, y.info->num_columns);
        if (x.hash != y.hash) return false;
        if (x.is_all_nulls()) {
            return y.is_all_nulls();
        } else if (y.is_all_nulls()) {
            return false;
        } else {
            for (int i = 0; i < x.info->num_columns; ++i) {
                if (x.info->columns[i]->equals(x.offset, *y.info->columns[i], y.offset) != Column::EQUALS_TRUE) {
                    return false;
                }
            }
        }
        return true;
    }
};

struct HashOnDedupColumnsKey {
    std::size_t operator()(const DedupColumnsKey& x) const { return x.hash; }
};

using ColumnsKeyHashMap = phmap::flat_hash_map<ColumnsKey, int32_t, HashOnColumnsKey, EqualOnColumnsKey>;
using ColumnsKeyHashSet = phmap::flat_hash_set<ColumnsKey, HashOnColumnsKey, EqualOnColumnsKey>;
using DedupColumnsKeyHashSet = phmap::flat_hash_set<DedupColumnsKey, HashOnDedupColumnsKey, EqualOnDedupColumnsKey>;

// input columns result in intermediate result: struct{array[outCol.field0], array[outCol.field1], ...,
// array[inCol.field0], ..., array[pkCol.fieldN], array[col3]... array[col8], varbinary}
struct CelonisEnumerateAggregateState {
    CelonisEnumerateAggregateState();
    ~CelonisEnumerateAggregateState();

    void update(FunctionContext* ctx, const Column** columns, size_t row_num, size_t size);
    size_t serialized_size(FunctionContext* ctx) const;
    void serialize(FunctionContext* ctx, uint8_t* dst) const;
    std::vector<bool> deserialize(FunctionContext* ctx, const uint8_t* src, size_t len);

    const Column* get_column(int column_num) const {
        if ((*data_column_index)[column_num] >= 0) {
            return (*data_raw_columns)[(*data_column_index)[column_num]];
        }
        return nullptr;
    }

    std::unique_ptr<Columns> data_columns = nullptr;
    std::unique_ptr<std::vector<const Column*>> data_raw_columns = nullptr;
    std::unique_ptr<std::vector<LogicalType>> logical_types = nullptr;
    std::unique_ptr<std::vector<int>> data_column_index = nullptr;
    int key_col_num = 0;

    // To deduplicate rows by (OUT_COLUMNS, IN_COLUMNS, [PK_COLUMNS]).
    ColumnsKey::CommonInfo columns_key_info;
    std::unique_ptr<DedupColumnsKeyHashSet> hash_set = nullptr;

    // Options with constant columns
    bool allow_cycles = false;
    RunTimeCppType<TYPE_TINYINT> length_comparison = 0;
    RunTimeCppType<TYPE_BIGINT> length = 0;

    // Used in finalization
    Enumerator* enumerator = nullptr;
};

/**
 * Mode::NODE_PATHS - celonis_enumerate_node_paths()
 * @param: [outColumns, inColumns, pkColumns, outStart, outEnd, inStart, inEnd, outAll, inAll, allowCycles, lengthComparison, length]
 * @paramType: [STRUCT | STRUCT | STRICT | BOOLEAN | BOOLEAN | BOOLEAN | BOOLEAN | BOOLEAN | BOOLEAN | BOOLEAN | VARCHAR | BIGINT]
 *   outColumns, inColumns : They must have same types.
 *   outStart, outEnd, inStart, inEnd, outAll, inAll : Optional.
 *   lengthComparison : VARCHAR [LESS, LESS_EQUAL, EQUAL, GREATER, GREATER_EQUAL, NOT_EQUAL]
 *   Rows are deduplicated by (outColumns, inColumns, pkColumns).
 * @return: Multiple rows of STRUCT { ARRAYs of outColumns/inColumns types }. Field names are copied from outColumns.
 *   Internally, outputs are generated by chunks.
 *
 * Implements PQL LINK_PATH
 * https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11250659/LINK+PATH
 * https://docs.google.com/document/d/12mY8VVafhNB-YaswTdjHgO6YdWAs699LgDl8v-zfj-U
 *
 * Mode::TRANSITIVE_EDGES - celonis_enumerate_TRANSITIVE_EDGES()
 * @param: [outColumns, inColumns, maxLength]
 * @paramType: [STRUCT | STRUCT | BIGINT]
 *   outColumns, inColumns : They must have same types.
 *   maxLength : If < 0, there is no limit.
 *   Rows are deduplicated by (outColumns, inColumns).
 * @return: STRUCT with two fields, "from" and "to", which are also STRUCT and their field names are copied from outColumns.
 *   Internally, outputs are generated by chunks.
 */
class CelonisEnumerateAggregateFunction
        : public AggregateFunctionBatchHelper<CelonisEnumerateAggregateState, CelonisEnumerateAggregateFunction> {
public:
    enum class Mode { NODE_PATHS, TRANSITIVE_EDGES };

    CelonisEnumerateAggregateFunction(Mode mode) : mode_(mode) {}

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override;

    void update_batch_single_state(FunctionContext* ctx, size_t chunk_size, const Column** columns,
                                   AggDataPtr __restrict state) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override {
        // Used for streaming aggregation passthrough. Not implemented.
        throw std::runtime_error(
                "convert_to_serialize_format is not supported. "
                "Not to trigger this, SET streaming_preaggregation_mode=\"force_preaggregation\"");
    }

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    std::string get_name() const override {
        if (mode_ == Mode::NODE_PATHS) {
            return "celonis_enumerate_node_paths";
        } else {
            return "celonis_enumerate_transitive_edges";
        }
    }

private:
    void initialize_constant_options(const FunctionContext* ctx, CelonisEnumerateAggregateState& state) const;

    void create_impl(FunctionContext* ctx, CelonisEnumerateAggregateState& state, std::vector<bool>* is_nulls) const;

    void update_impl(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state, size_t row_num,
                     size_t size) const;

    Mode mode_;
};

} // namespace starrocks
