#include "enumerate_node_paths.h"

#include <cmath>
#include <fmt/format.h>

#include "column/array_column.h"
#include "column/binary_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exec/sorting/sorting.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "gutil/casts.h"
#include "runtime/runtime_state.h"
#include "util/utf8.h"

namespace starrocks {

namespace {

enum InputColumnIndex {
    OUT_COLUMNS, IN_COLUMNS, PK_COLUMNS, OUT_START, OUT_END, IN_START, IN_END, OUT_ALL, IN_ALL, ALLOW_CYCLES,
    LENGTH_COMPARISON, LENGTH, NUMBER_OF_COLUMNS
};

enum LengthComparison {
    LESS, LESS_EQUAL, EQUAL, GREATER, GREATER_EQUAL, NOT_EQUAL
};

LengthComparison parseLengthComparison(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    if (str == "LESS") return LengthComparison::LESS;
    if (str == "LESS_EQUAL") return LengthComparison::LESS_EQUAL;
    if (str == "EQUAL") return LengthComparison::EQUAL;
    if (str == "GREATER") return LengthComparison::GREATER;
    if (str == "GREATER_EQUAL") return LengthComparison::GREATER_EQUAL;
    if (str == "NOT_EQUAL") return LengthComparison::NOT_EQUAL;
    throw std::runtime_error(fmt::format("Length comparison '{}' not supported", str));
}

class ColumnsKeyDictionary {
public:
    enum ColumnsInfoType { OUT, IN, NUMBER_OF_TYPES };
    ColumnsKeyDictionary(FunctionContext* ctx, const Column** columns, LogicalType* logical_types) {
        int32_t num_columns = ctx->get_arg_type(0)->children.size();
        columns_key_info_[ColumnsInfoType::OUT] =
                {.columns = columns, .types = logical_types, .num_columns = num_columns};
        columns_key_info_[ColumnsInfoType::IN] =
                {.columns = columns + num_columns, .types = logical_types, .num_columns = num_columns};
        // Assign ID 0 to all NULLs.
        may_add_key(ColumnsInfoType::OUT, -1);
    }

    int32_t may_add_key(ColumnsInfoType type, int32_t offset) {
        ColumnsKey key{&columns_key_info_[type], offset};
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            return it->second;
        }
        auto idx = key_map_.size();
        key_map_.insert({key, idx});
        keys_.emplace_back(type, offset);
        return idx;
    }

    ColumnsKey key(int index) {
        auto [type, offset] = keys_[index];
        return {&columns_key_info_[type], offset};
    }

    int32_t offset(int index) {
        auto [type, offset] = keys_[index];
        return offset;
    }

private:
    ColumnsKey::CommonInfo columns_key_info_[ColumnsInfoType::NUMBER_OF_TYPES];
    ColumnsKeyHashMap key_map_;
    std::vector<std::pair<ColumnsInfoType, int32_t>> keys_;
};

} // namespace

class EnumeratorImplBase {
public:
    virtual ~EnumeratorImplBase() = default;
    virtual void Enumerate(Column* to) = 0;
};

template <bool allow_cycles>
class NodePathEnumerator : public EnumeratorImplBase {
public:
    struct EdgeInfo {
        explicit EdgeInfo(int32_t in_idx) : in_index(in_idx) {}
        int32_t in_index;
    };

    NodePathEnumerator(FunctionContext* ctx, const CelonisEnumerateAggregateState& state);
    ~NodePathEnumerator() override = default;

    // Enumerates paths up to maximum chunk size and writes the results to `to`.
    void Enumerate(Column* to) override;

private:
    // Traverses to the next path to consider. path_ will be empty if there is no next path available.
    void NextPath();

    int key_size_;
    LengthComparison length_comparison_;
    int64_t length_;

    ColumnsKeyDictionary ckd_; // Dictionary of nodes. Index is used below for traversing paths.
    phmap::flat_hash_map<int32_t, std::vector<EdgeInfo>, StdHash<int32_t>> edges_map_; // Key: OutIndex, Value: EdgeInfo
    HashSet<int32_t> is_end_;
    HashSet<int32_t> is_not_all_;

    std::vector<EdgeInfo> stack_;
    std::vector<int32_t> path_; // The current path
    std::vector<int32_t> path_pk_offset_; // PK offsets in the current path. Used when allow_cycles is true.
    HashSet<int32_t> visited_; // To check if a node (or a pk when allow_cycles is true) has been visited.
};

template <>
struct NodePathEnumerator<true>::EdgeInfo {
    explicit EdgeInfo(int32_t in_idx) : in_index(in_idx), pk_offset(-1) {}
    EdgeInfo(int32_t in_idx, int32_t pk_off) : in_index(in_idx), pk_offset(pk_off) {}
    int32_t in_index;
    // Instead of building a dictionary, we can just use an offset as an index of PK_COLUMNS.
    int32_t pk_offset;
};

template <bool allow_cycles>
NodePathEnumerator<allow_cycles>::NodePathEnumerator(FunctionContext* ctx, const CelonisEnumerateAggregateState& state)
        : key_size_(ctx->get_arg_type(InputColumnIndex::OUT_COLUMNS)->children.size()),
          length_comparison_(static_cast<LengthComparison>(state.length_comparison)),
          length_(state.length),
          ckd_(ctx, state.data_raw_columns->data(), state.logical_types->data()) {
    const Column* out_start = state.get_column(InputColumnIndex::OUT_START);
    const Column* out_end = state.get_column(InputColumnIndex::OUT_END);
    const Column* in_start = state.get_column(InputColumnIndex::IN_START);
    const Column* in_end = state.get_column(InputColumnIndex::IN_END);
    const Column* out_all = state.get_column(InputColumnIndex::OUT_ALL);
    const Column* in_all = state.get_column(InputColumnIndex::IN_ALL);

    // To find implicit start
    HashSet<int32_t> outs;
    HashSet<int32_t> ins;
    phmap::flat_hash_map<int32_t, std::vector<int32_t>, StdHash<int32_t>> reverse_edges;
    // To keep explicit start
    HashSet<int32_t> is_start;

    auto elem_size = (*state.data_columns)[InputColumnIndex::OUT_COLUMNS]->size();
    for (int32_t row = 0; row < elem_size; ++row) {
        auto out_idx = ckd_.may_add_key(ColumnsKeyDictionary::ColumnsInfoType::OUT, row);
        auto in_idx = ckd_.may_add_key(ColumnsKeyDictionary::ColumnsInfoType::IN, row);
        outs.insert(out_idx);
        ins.insert(in_idx);
        if constexpr (allow_cycles) {
            edges_map_[out_idx].emplace_back(in_idx, row);
        } else {
            edges_map_[out_idx].emplace_back(in_idx);
        }
        reverse_edges[in_idx].push_back(out_idx);
        if (out_idx != 0) {
            if (out_start && out_start->get(row).get<bool>()) {
                is_start.insert(out_idx);
            }
            if (out_end && out_end->get(row).get<bool>()) {
                is_end_.insert(out_idx);
            }
            if (out_all && !out_all->get(row).get<bool>()) {
                is_not_all_.insert(out_idx);
            }
        }
        if (in_idx != 0) {
            if (in_start && in_start->get(row).get<bool>()) {
                is_start.insert(in_idx);
            }
            if (in_end && in_end->get(row).get<bool>()) {
                is_end_.insert(in_idx);
            }
            if (in_all && !in_all->get(row).get<bool>()) {
                is_not_all_.insert(in_idx);
            }
        }
    }
    if (out_end != nullptr || in_end != nullptr) {
        if (is_end_.empty()) {
            // We don't need to initialize the stack because no path exists.
            return;
        }
    }
    if (out_start == nullptr && in_start == nullptr) {
        // Implicit Start
        for (auto out_idx: outs) {
            if (out_idx != 0 && !ins.contains(out_idx)) {
                stack_.emplace_back(out_idx);
            }
        }
        // Finds a single node path with (OUT:NULL, IN:in_idx).
        for (const auto& [in_idx, out_idxes]: reverse_edges) {
            if (in_idx != 0 && out_idxes.size() == 1 && out_idxes[0] == 0) {
                stack_.emplace_back(in_idx);
            }
        }
    } else {
        for (auto idx : is_start) {
            if (idx != 0) {
                stack_.emplace_back(idx);
            }
        }
    }
}

template <bool allow_cycles>
void NodePathEnumerator<allow_cycles>::NextPath() {
    while (!stack_.empty()) {
        auto edge_info = stack_.back();
        stack_.pop_back();
        if (edge_info.in_index > 0) {
            path_.push_back(edge_info.in_index);
            if constexpr (allow_cycles) {
                path_pk_offset_.push_back(edge_info.pk_offset);
                DCHECK(!visited_.contains(edge_info.pk_offset));
                visited_.insert(edge_info.pk_offset);
            } else {
                DCHECK(!visited_.contains(edge_info.in_index));
                visited_.insert(edge_info.in_index);
            }
            stack_.emplace_back(-1);
            return;
        }
        DCHECK_NE(edge_info.in_index, 0);
        DCHECK(!path_.empty());
        auto in_index = path_.back();
        path_.pop_back();
        if constexpr (allow_cycles) {
            auto pk_offset = path_pk_offset_.back();
            path_pk_offset_.pop_back();
            visited_.erase(pk_offset);
        } else {
            visited_.erase(in_index);
        }
    }
    DCHECK(path_.empty());
    DCHECK(path_pk_offset_.empty());
}

template <bool allow_cycles>
void NodePathEnumerator<allow_cycles>::Enumerate(Column* to) {
    auto max_chunk_size = config::vector_chunk_size;
    std::vector<std::vector<int32_t>> results;
    auto may_add_path_to_results = [this, &results](bool* checked) {
        if (*checked) return;
        int length = this->path_.size();
        if ((this->length_comparison_ == LESS && length < this->length_) ||
            (this->length_comparison_ == LESS_EQUAL &&  length <= this->length_) ||
            (this->length_comparison_ == EQUAL && length == this->length_) ||
            (this->length_comparison_ == GREATER && length > this->length_) ||
            (this->length_comparison_ == GREATER_EQUAL && length >= this->length_) ||
            (this->length_comparison_ == NOT_EQUAL && length != this->length_)) {
            results.push_back(this->path_);
        }
        *checked = true;
    };

    while (results.size() < max_chunk_size) {
        NextPath();
        if (path_.empty()) {
            break;
        }
        int32_t pathLast = path_.back();
        DCHECK_NE(pathLast, 0);
        if (is_not_all_.contains(pathLast)) {
            continue;
        }
        bool checked = false;
        if (is_end_.contains(pathLast)) {
            may_add_path_to_results(&checked);
        }
        auto it = edges_map_.find(pathLast);
        if (it == edges_map_.end()) {
            if (is_end_.empty())  {
                may_add_path_to_results(&checked);
            }
            continue;
        }
        // Stops traversing further if the length constraint has been met already.
        if ((length_comparison_ == LESS && path_.size() + 1 == length_) ||
            ((length_comparison_ == LESS_EQUAL || length_comparison_ == EQUAL) && path_.size() == length_)) {
            continue;
        }
        int prev_node = -1;
        for (auto edge: it->second) {
            // We visit a node once.
            if (edge.in_index == prev_node) {
                continue;
            }

            if (edge.in_index == 0) {
                if (is_end_.empty())  {
                    may_add_path_to_results(&checked);
                }
                continue;
            }
            if constexpr (allow_cycles) {
                if (visited_.contains(edge.pk_offset)) {
                    continue;
                }
            } else {
                if (visited_.contains(edge.in_index)) {
                    continue;
                }
            }
            prev_node = edge.in_index;
            stack_.push_back(edge);
        }
    }
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    std::vector<ArrayColumn*> array_columns(key_size_);
    for (int i = 0; i < key_size_; ++i) {
        array_columns[i] = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[i].get()));
    }
    DCHECK_EQ(fields.size(), key_size_);
    for (const auto& result : results) {
        std::vector<DatumArray> arrays;
        for (int i = 0; i < key_size_; ++i) {
            arrays.emplace_back();
            arrays.back().reserve(result.size());
        }
        for (auto idx : result) {
            auto key = ckd_.key(idx);
            for (int i = 0; i < key_size_; ++i) {
                arrays[i].push_back(key.get(i));
            }
        }
        for (int i = 0; i < key_size_; ++i) {
            array_columns[i]->append_datum(arrays[i]);
        }
    }
    for (int i = 0; i < key_size_; ++i) {
        if (fields[i]->is_nullable()) {
            down_cast<NullableColumn*>(fields[i].get())->mutable_null_column()->append_default(results.size());
        }
    }
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->mutable_null_column()->append_default(results.size());
    }
}

class TransitiveEdgeEnumerator : public EnumeratorImplBase {
public:
    TransitiveEdgeEnumerator(FunctionContext* ctx, const CelonisEnumerateAggregateState& state);
    ~TransitiveEdgeEnumerator() override = default;

    // Enumerates paths up to maximum chunk size and writes the results to `to`.
    void Enumerate(Column* to) override;

private:
    // Traverses to the next path to consider. path_ will be empty if there is no next path available.
    void NextPath();

    int key_size_;
    int64_t max_length_;

    ColumnsKeyDictionary ckd_; // Dictionary of nodes. Index is used below for traversing paths.
    phmap::flat_hash_map<int32_t, HashSet<int32_t>, StdHash<int32_t>> edges_map_; // Key: OutIndex, Value: InIndex

    std::vector<int32_t> stack_;
    std::vector<int32_t> path_; // The current path
    phmap::flat_hash_map<int32_t, size_t, StdHash<int32_t>> visit_info_; // The distance when a node is added for traversing.
    HashSet<int32_t> exported_; // To check if a pair has been exported.
};

TransitiveEdgeEnumerator::TransitiveEdgeEnumerator(FunctionContext* ctx, const CelonisEnumerateAggregateState& state)
        : key_size_(ctx->get_arg_type(InputColumnIndex::OUT_COLUMNS)->children.size()),
          max_length_(state.length),
          ckd_(ctx, state.data_raw_columns->data(), state.logical_types->data()) {
    HashSet<int32_t> from;

    auto elem_size = (*state.data_columns)[InputColumnIndex::OUT_COLUMNS]->size();
    for (int32_t row = 0; row < elem_size; ++row) {
        auto out_idx = ckd_.may_add_key(ColumnsKeyDictionary::ColumnsInfoType::OUT, row);
        auto in_idx = ckd_.may_add_key(ColumnsKeyDictionary::ColumnsInfoType::IN, row);
        if (out_idx != 0) {
            from.insert(out_idx);
            if (in_idx != 0) {
                edges_map_[out_idx].insert(in_idx);
            }
        }
        if (in_idx != 0) {
            from.insert(in_idx);
        }
    }
    for (auto idx : from) {
        stack_.emplace_back(idx);
    }
}

void TransitiveEdgeEnumerator::NextPath() {
    while (!stack_.empty()) {
        auto in_index = stack_.back();
        stack_.pop_back();
        if (in_index > 0) {
            path_.push_back(in_index);
            stack_.emplace_back(-1);
            if (path_.size() == 1) {
                visit_info_[in_index] = 0;
            }
            return;
        } else {
            DCHECK_NE(in_index, 0);
            DCHECK(!path_.empty());
            path_.pop_back();
            if (path_.empty()) {
                visit_info_.clear();
                exported_.clear();
            }
        }
    }
    DCHECK(path_.empty());
}


void TransitiveEdgeEnumerator::Enumerate(Column* to) {
    if (max_length_ == 0) {
        to->append_default();
        return;
    }
    std::vector<std::pair<int32_t, int32_t>> results;
    auto max_chunk_size = config::vector_chunk_size;
    while (results.size() < max_chunk_size) {
        NextPath();
        if (path_.empty()) {
            break;
        }
        int32_t pathLast = path_.back();
        DCHECK_NE(pathLast, 0);

        if (!exported_.contains(pathLast)) {
            results.emplace_back(this->path_.front(), pathLast);
            exported_.insert(pathLast);
        }
        // Stops traversing further if the length constraint has been met.
        if (path_.size() == max_length_) {
            continue;
        }
        auto edge_it = edges_map_.find(pathLast);
        if (edge_it == edges_map_.end()) {
            continue;
        }
        for (auto in_index : edge_it->second) {
            auto it = visit_info_.find(in_index);
            if (it == visit_info_.end()) {
                stack_.push_back(in_index);
                visit_info_.emplace(in_index, path_.size());
            } else if (it->second > path_.size()) {
                // We have visited the node but we may not be able to visit all following paths due to maxLength.
                // So we traverse from the node again. If further optimization is necessary, we may check if that is
                // really the case.
                stack_.push_back(in_index);
                it->second = path_.size();
            }
        }
    }
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    DCHECK_EQ(fields.size(), 2);
    auto& from_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()))->fields_column();
    DCHECK_EQ(from_fields.size(), key_size_);
    auto& to_fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()))->fields_column();
    DCHECK_EQ(to_fields.size(), key_size_);

    for (const auto& result : results) {
        auto from_key = ckd_.key(result.first);
        auto to_key = ckd_.key(result.second);
        for (int i = 0; i < key_size_; ++i) {
            from_fields[i]->append_datum(from_key.get(i));
            to_fields[i]->append_datum(to_key.get(i));
        }
    }
    if (fields[0]->is_nullable()) {
        down_cast<NullableColumn*>(fields[0].get())->mutable_null_column()->append_default(results.size());
    }
    if (fields[1]->is_nullable()) {
        down_cast<NullableColumn*>(fields[1].get())->mutable_null_column()->append_default(results.size());
    }
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->mutable_null_column()->append_default(results.size());
    }
}

class Enumerator {
public:
    Enumerator() = default;
    ~Enumerator() {
        delete impl_;
    }

    void Enumerate(FunctionContext* ctx, const CelonisEnumerateAggregateState& state,
                   CelonisEnumerateAggregateFunction::Mode mode, Column* to) {
        if (impl_ == nullptr) {
            if (mode == CelonisEnumerateAggregateFunction::Mode::NODE_PATHS) {
                if (state.allow_cycles && state.get_column(InputColumnIndex::PK_COLUMNS) != nullptr) {
                    impl_ = new NodePathEnumerator<true>(ctx, state);
                } else {
                    impl_ = new NodePathEnumerator<false>(ctx, state);
                }
            } else {
                DCHECK(mode == CelonisEnumerateAggregateFunction::Mode::TRANSITIVE_EDGES);
                impl_ = new TransitiveEdgeEnumerator(ctx, state);
            }
        }
        impl_->Enumerate(to);
    }

private:
    EnumeratorImplBase* impl_ = nullptr;
};

CelonisEnumerateAggregateState::CelonisEnumerateAggregateState() : enumerator(new Enumerator()) {
}

CelonisEnumerateAggregateState::~CelonisEnumerateAggregateState() {
    if (data_columns != nullptr) {
        for (auto& col : *data_columns) {
            col.reset();
        }
        data_columns->clear();
        data_columns.reset(nullptr);
    }
    data_column_index.reset(nullptr);
    delete enumerator;
}

void CelonisEnumerateAggregateState::update(FunctionContext* ctx, const Column** columns, size_t row_num, size_t size) {
    DCHECK(data_columns != nullptr);
    ColumnsKey::CommonInfo input_columns_key_info{
            .columns = columns, .types = logical_types->data(), .num_columns = key_col_num};
    for (int32_t offset = row_num; offset < row_num + size; ++offset) {
        // Deduplicate rows.
        DedupColumnsKey tmp_key{&input_columns_key_info, offset};
        if (hash_set->contains(tmp_key)) {
            continue;
        }
        for (int j = 0; j < data_columns->size(); ++j) {
            // Optional BOOLEAN columns shouldn't have NULL values. If there is NULL, maps to false.
            if (j >= key_col_num && columns[j]->is_null(offset)) {
                (*data_columns)[j]->append_datum(false);
            } else {
                (*data_columns)[j]->append_datum(columns[j]->get(offset));
            }
        }
        hash_set->emplace(&columns_key_info, static_cast<int32_t>((*data_columns)[0]->size()) - 1);
    }
}

size_t CelonisEnumerateAggregateState::serialized_size(FunctionContext* ctx) const {
    size_t result = 0;
    result += ctx->get_num_args() * sizeof(uint8_t); // Is null literal per each column
    result += sizeof(uint8_t);                       // Allow cycles
    result += sizeof(RunTimeCppType<TYPE_TINYINT>);  // Length comparison
    result += sizeof(RunTimeCppType<TYPE_BIGINT>);   // Length
    return result;
}

void CelonisEnumerateAggregateState::serialize(FunctionContext* ctx, uint8_t* dst) const {
    for (int i = 0; i < ctx->get_num_args(); ++i) {
        uint8_t is_null = 0;
        if (ctx->is_constant_column(i) && ctx->get_constant_column(i)->is_null(0)) {
            is_null = 1;
        }
        memcpy(dst, &is_null, sizeof(uint8_t));
        dst += sizeof(uint8_t);
    }
    memcpy(dst, &allow_cycles, sizeof(uint8_t));
    dst += sizeof(uint8_t);
    memcpy(dst, &length_comparison, sizeof(RunTimeCppType<TYPE_TINYINT>));
    dst += sizeof(RunTimeCppType<TYPE_TINYINT>);
    memcpy(dst, &length, sizeof(RunTimeCppType<TYPE_BIGINT>));
    dst += sizeof(RunTimeCppType<TYPE_BIGINT>);
}

std::vector<bool> CelonisEnumerateAggregateState::deserialize(FunctionContext* ctx, const uint8_t* src, size_t len) {
    const uint8_t* end = src + len;
    std::vector<bool> is_nulls;
    is_nulls.reserve(ctx->get_num_args());
    for (int i = 0; i < ctx->get_num_args(); ++i) {
        uint8_t is_null;
        memcpy(&is_null, src, sizeof(uint8_t));
        src += sizeof(uint8_t);
        is_nulls.push_back(is_null);
    }
    uint8_t ac;
    memcpy(&ac, src, sizeof(uint8_t));
    src += sizeof(uint8_t);
    allow_cycles = ac;
    memcpy(&length_comparison, src, sizeof(RunTimeCppType<TYPE_TINYINT>));
    src += sizeof(RunTimeCppType<TYPE_TINYINT>);
    memcpy(&length, src, sizeof(RunTimeCppType<TYPE_BIGINT>));
    src += sizeof(RunTimeCppType<TYPE_BIGINT>);

    DCHECK_EQ(src, end);
    return is_nulls;
}

void CelonisEnumerateAggregateFunction::create_impl(FunctionContext* ctx, CelonisEnumerateAggregateState& state,
                                                    std::vector<bool>* is_nulls) const {
    int num_of_columns = mode_ == Mode::NODE_PATHS ? InputColumnIndex::NUMBER_OF_COLUMNS : 3;
    int num_of_key_columns = mode_ == Mode::NODE_PATHS ? 3 : 2;
    DCHECK_EQ(ctx->get_num_args(), num_of_columns);
    DCHECK(is_nulls == nullptr || is_nulls->size() == num_of_columns);

    state.data_columns = std::make_unique<Columns>();
    state.data_raw_columns = std::make_unique<std::vector<const Column*>>();
    state.logical_types = std::make_unique<std::vector<LogicalType>>();
    state.data_column_index = std::make_unique<std::vector<int>>(num_of_columns, -1);

    auto is_null = [ctx, is_nulls](int idx) -> bool {
        if (is_nulls == nullptr) {
            return ctx->is_constant_column(idx) && ctx->get_constant_column(idx)->is_null(0);
        } else {
            return (*is_nulls)[idx];
        }
    };

    // Key columns: outColumns, inColumns, pkColumns
    for (int i = 0; i < num_of_key_columns; ++i) {
        auto type_desc = *ctx->get_arg_type(i);
        if (i == InputColumnIndex::PK_COLUMNS && is_null(i)) {
            continue;
        }
        (*state.data_column_index)[i] = state.data_columns->size();
        for (auto& td : type_desc.children) {
            state.data_columns->emplace_back(ctx->create_column(td, true));
            state.logical_types->push_back(td.type);
        }
    }
    state.key_col_num = state.data_columns->size();

    if (mode_ == Mode::NODE_PATHS) {
        // Optional columns
        for (int i = 3; i < 9; ++i) {
            auto type_desc = *ctx->get_arg_type(i);
            if (is_null(i)) {
                continue;
            }
            (*state.data_column_index)[i] = state.data_columns->size();
            // Creates non-nullable columns. Null values will be mapped to false.
            state.data_columns->emplace_back(ctx->create_column(type_desc, false));
            state.logical_types->push_back(type_desc.type);
        }
        // Non-optional constant columns. Types are defined in FunctionSet.java.
        if (is_nulls == nullptr) {
            state.allow_cycles = ColumnHelper::get_const_value<TYPE_BOOLEAN>(
                    ctx->get_constant_column(InputColumnIndex::ALLOW_CYCLES));
            state.length_comparison = parseLengthComparison(ColumnHelper::get_const_value<TYPE_VARCHAR>(
                    ctx->get_constant_column(InputColumnIndex::LENGTH_COMPARISON)).to_string());
            state.length = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(InputColumnIndex::LENGTH));
        }
    } else {
        DCHECK(mode_ == Mode::TRANSITIVE_EDGES);
        if (is_nulls == nullptr) {
            state.length = ColumnHelper::get_const_value<TYPE_BIGINT>(ctx->get_constant_column(2));
        }
    }

    for (const auto& column : *state.data_columns) {
        state.data_raw_columns->push_back(column.get());
    }

    state.columns_key_info.columns = state.data_raw_columns->data();
    state.columns_key_info.types = state.logical_types->data();
    state.columns_key_info.num_columns = state.key_col_num;
    state.hash_set = std::make_unique<DedupColumnsKeyHashSet>();
}

void CelonisEnumerateAggregateFunction::reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state)
        const {
    auto& state_impl = this->data(state);
    if (state_impl.data_columns != nullptr) {
        for (auto& col : *state_impl.data_columns) {
            col->resize(0);
        }
    }
}

void CelonisEnumerateAggregateFunction::update_impl(FunctionContext* ctx, const Column** columns,
                                                    AggDataPtr __restrict state, size_t row_num, size_t size) const {
    auto& state_impl = this->data(state);
    if (state_impl.data_columns == nullptr) {
        create_impl(ctx, state_impl, nullptr);
    }
    std::vector<const Column*> flattened_columns;
    for (int i = 0; i < 3; ++i) {
        if (i == 2 && (mode_ == Mode::TRANSITIVE_EDGES || (*state_impl.data_column_index)[i] < 0)) {
            break;
        }
        DCHECK_EQ((*state_impl.data_column_index)[i], flattened_columns.size());
        auto& fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[i]))->fields();
        for (const auto& field : fields) {
            flattened_columns.push_back(field.get());
        }
    }
    if (mode_ == Mode::NODE_PATHS) {
        for (int i = 3; i < 9; ++i) {
            if ((*state_impl.data_column_index)[i] < 0) {
                continue;
            }
            DCHECK_EQ((*state_impl.data_column_index)[i], flattened_columns.size());
            flattened_columns.push_back(columns[i]);
        }
    }
    this->data(state).update(ctx, flattened_columns.data(), row_num, size);
}

void CelonisEnumerateAggregateFunction::update(FunctionContext* ctx, const Column** columns,
                                               AggDataPtr __restrict state, size_t row_num) const {
    update_impl(ctx, columns,state, row_num, 1);
}

void CelonisEnumerateAggregateFunction::update_batch_single_state(FunctionContext* ctx, size_t chunk_size,
                                                                  const Column** columns, AggDataPtr __restrict state)
        const {
    update_impl(ctx, columns,state, 0, chunk_size);
}

void CelonisEnumerateAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                              size_t row_num) const {
    if (column->is_nullable() && column->is_null(row_num)) {
        return;
    }
    auto& fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    if (state_impl.data_columns == nullptr) {
        // Const columns in merge is broken. We need to pass info about null columns and constant columns.
        const auto* last_field = down_cast<const BinaryColumn*>(ColumnHelper::get_data_column(fields.back().get()));
        Slice slice = last_field->get_slice(row_num);
        auto is_nulls = state_impl.deserialize(ctx, (const uint8_t*)slice.data, slice.size);
        create_impl(ctx, state_impl, &is_nulls);
    }

    auto num_columns = state_impl.data_columns->size();
    DCHECK_EQ(num_columns, fields.size() - 1);
    std::vector<const Column*> columns;
    columns.reserve(num_columns);
    size_t offset = 0;
    size_t count = 0;
    for (auto i = 0; i < num_columns; ++i) {
        auto array_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(fields[i].get()));
        columns.push_back(array_column->elements_column().get());
        auto& offsets = array_column->offsets().get_data();
        if (i == 0) {
            offset = offsets[row_num];
            count = offsets[row_num + 1] - offsets[row_num];
        } else {
            DCHECK_EQ(offset, offsets[row_num]);
            DCHECK_EQ(count, offsets[row_num + 1] - offsets[row_num]);
        }
    }
    this->data(state).update(ctx, columns.data(), offset, count);
}

void CelonisEnumerateAggregateFunction::serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                            Column* to) const {
    auto& state_impl = this->data(state);
    if (state_impl.data_columns == nullptr || (*state_impl.data_columns)[0]->size() == 0) {
        to->append_default();
        return;
    }
    auto& fields = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
    }
    auto num_columns = state_impl.data_columns->size();
    DCHECK_EQ(fields.size() - 1, num_columns);
    for (auto i = 0; i < num_columns; ++i) {
        auto elem_size = (*state_impl.data_columns)[i]->size();
        auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(fields[i].get()));
        if (fields[i]->is_nullable()) {
            down_cast<NullableColumn*>(fields[i].get())->null_column_data().emplace_back(0);
        }
        array_col->elements_column()->append(*(*state_impl.data_columns)[i], 0, elem_size);
        auto& offsets = array_col->offsets_column()->get_data();
        offsets.push_back(offsets.back() + elem_size);
    }
    // Serialize remaining information
    BinaryColumn* binary_column;
    if (fields[num_columns]->is_nullable()) {
        auto nullable_column = down_cast<NullableColumn*>(fields[num_columns].get());
        nullable_column->null_column_data().emplace_back(0);
        binary_column = down_cast<BinaryColumn*>(nullable_column->mutable_data_column());
    } else {
        binary_column = down_cast<BinaryColumn*>(fields[num_columns].get());
    }
    size_t old_size = binary_column->get_bytes().size();
    size_t new_size = old_size + state_impl.serialized_size(ctx);
    binary_column->get_bytes().resize(new_size);
    state_impl.serialize(ctx, binary_column->get_bytes().data() + old_size);
    binary_column->get_offset().emplace_back(new_size);
}

void CelonisEnumerateAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                           Column* to) const {
    auto& state_impl = this->data(state);
    if (state_impl.data_columns == nullptr || (*state_impl.data_columns)[0]->size() == 0) {
        return;
    }

    state_impl.enumerator->Enumerate(ctx, state_impl, mode_, to);
}

} // namespace starrocks
