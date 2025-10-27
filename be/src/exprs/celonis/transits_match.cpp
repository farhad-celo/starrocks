#include "exprs/celonis/transits_match.h"

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/struct_column.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "util/phmap/btree.h"

namespace starrocks {

namespace {

struct TransitsMatchStateFragmentLocal {
    ScalarFunction function;
    std::optional<phmap::btree_map<DatumKey, std::set<DatumKey>>> manual_map = std::nullopt;
    // Only one of left_manual and right_manual is NULL; left_manual and right_manual have different length;
    // left_manual or right_manual contains NULL
    bool is_malformed = false;
};

struct Edge {
    size_t left_index;
    size_t right_index;
};

void AppendFields(const Columns& source_fields, std::vector<DatumArray>& arrays, size_t row, size_t index) {
    const auto n_fields = source_fields.size();
    for (auto i = 0; i < n_fields; ++i) {
        arrays[i].push_back(source_fields[i]->get(row).get_array()[index]);
    }
}

void AddEdges(const std::vector<Edge>& edges, const Columns& left_key_fields, const Columns& right_key_fields,
              Columns& res_left_fields, Columns& res_right_fields, NullableColumn* null_column, size_t row) {
    const auto n_left_fields = left_key_fields.size();
    const auto n_right_fields = right_key_fields.size();
    std::vector<DatumArray> left_arrays;
    std::vector<DatumArray> right_arrays;
    for (auto i = 0; i < n_left_fields; ++i) {
        DatumArray array;
        array.reserve(edges.size());
        left_arrays.push_back(array);
    }
    for (auto i = 0; i < n_right_fields; ++i) {
        DatumArray array;
        array.reserve(edges.size());
        right_arrays.push_back(array);
    }
    for (const auto& edge : edges) {
        AppendFields(left_key_fields, left_arrays, row, edge.left_index);
        AppendFields(right_key_fields, right_arrays, row, edge.right_index);
    }
    null_column->null_column_data().emplace_back(0);
    for (auto i = 0; i < n_left_fields; ++i) {
        res_left_fields[i]->append_datum(left_arrays[i]);
    }
    for (auto i = 0; i < n_right_fields; ++i) {
        res_right_fields[i]->append_datum(right_arrays[i]);
    }
}

std::optional<phmap::btree_map<DatumKey, std::set<DatumKey>>> build_map(
        const std::optional<DatumArray>& left_manual_array, const std::optional<DatumArray>& right_manual_array) {
    if (!left_manual_array.has_value() || !right_manual_array.has_value()) {
        return std::nullopt;
    }
    phmap::btree_map<DatumKey, std::set<DatumKey>> rv;
    const auto size = left_manual_array->size();
    DCHECK_EQ(size, right_manual_array->size());
    for (auto i = 0; i < size; ++i) {
        rv[left_manual_array.value()[i].convert2DatumKey()].insert(right_manual_array.value()[i].convert2DatumKey());
    }
    return rv;
}

std::vector<Edge> compute_edges(const DatumArray& left_match_array, const DatumArray& right_match_array,
                                const std::optional<phmap::btree_map<DatumKey, std::set<DatumKey>>>& manual_map) {
    std::vector<Edge> edges;
    const auto left_size = left_match_array.size();
    const auto right_size = right_match_array.size();
    phmap::btree_map<DatumKey, std::vector<size_t>> right_key_to_indexes;
    for (size_t i = 0; i < right_size; ++i) {
        right_key_to_indexes[right_match_array[i].convert2DatumKey()].push_back(i);
    }
    for (size_t i = 0; i < left_size; ++i) {
        const auto left_datum_key = left_match_array[i].convert2DatumKey();
        // compute matched keys
        std::set matched_keys = {left_datum_key};
        if (manual_map.has_value()) {
            auto it = manual_map->find(left_datum_key);
            if (it != manual_map->end()) {
                matched_keys = it->second;
            } else {
                matched_keys = {};
            }
        }
        for (auto matched_key : matched_keys) {
            auto it = right_key_to_indexes.find(matched_key);
            if (it != right_key_to_indexes.end()) {
                for (auto j : it->second) {
                    edges.push_back({i, j});
                }
            }
        }
    }
    return edges;
}

} // namespace

Status CelonisTransitsMatch::prepare(starrocks::FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    auto state = new TransitsMatchStateFragmentLocal();
    context->set_function_state(scope, state);

    auto left_manual_column = context->get_constant_column(4);
    auto right_manual_column = context->get_constant_column(5);

    if (left_manual_column == nullptr || right_manual_column == nullptr) {
        state->function = transits_match_non_constant_manual;
        return Status::OK();
    }
    state->function = transits_match_constant_manual;
    if (left_manual_column->empty() || right_manual_column->empty()) {
        return Status::OK();
    }
    if (left_manual_column->get(0).is_null() != right_manual_column->get(0).is_null()) {
        state->is_malformed = true;
        return Status::OK();
    }
    if (left_manual_column->get(0).is_null() && right_manual_column->get(0).is_null()) {
        return Status::OK();
    }
    auto left_manual_array = left_manual_column->get(0).get_array();
    auto right_manual_array = right_manual_column->get(0).get_array();
    if (left_manual_array.size() != right_manual_array.size()) {
        state->is_malformed = true;
        return Status::OK();
    }
    const auto size = left_manual_array.size();
    for (auto i = 0; i < size; ++i) {
        if (left_manual_array[i].is_null() || right_manual_array[i].is_null()) {
            state->is_malformed = true;
            return Status::OK();
        }
    }
    state->manual_map = build_map(left_manual_array, right_manual_array);
    return Status::OK();
}

Status CelonisTransitsMatch::close(starrocks::FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const TransitsMatchStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisTransitsMatch::transits_match_non_constant_manual(
        [[maybe_unused]] starrocks::FunctionContext* context, const starrocks::Columns& columns) {
    const size_t n_rows = columns[0]->size();
    auto& left_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto& right_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[2].get()))->fields();

    ColumnPtr res = context->create_column(context->get_return_type(), true);
    auto null_column = down_cast<NullableColumn*>(res.get());
    StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
    auto fields = st->fields_column();
    DCHECK_EQ(2, fields.size());
    StructColumn* res_left_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    StructColumn* res_right_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    auto res_left_fields = res_left_column->fields_column();
    auto res_right_fields = res_right_column->fields_column();
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row) ||
            columns[3]->is_null(row) || left_key_fields.size() == 0 || right_key_fields.size() == 0 ||
            (columns[4]->is_null(row) != columns[5]->is_null(row))) {
            res->append_nulls(1);
            continue;
        }

        const auto left_length = left_key_fields[0]->get(row).get_array().size();
        bool inconsistent_left_length = false;
        for (auto i = 0; i < left_key_fields.size(); ++i) {
            if (left_key_fields[i]->get(row).get_array().size() != left_length) {
                inconsistent_left_length = true;
                break;
            }
        }
        if (inconsistent_left_length) {
            res->append_nulls(1);
            continue;
        }

        const auto right_length = right_key_fields[0]->get(row).get_array().size();
        bool inconsistent_right_length = false;
        for (auto i = 0; i < right_key_fields.size(); ++i) {
            if (right_key_fields[i]->get(row).get_array().size() != right_length) {
                inconsistent_right_length = true;
                break;
            }
        }
        if (inconsistent_right_length) {
            res->append_nulls(1);
            continue;
        }

        auto left_match_array = columns[1]->get(row).get_array();
        auto right_match_array = columns[3]->get(row).get_array();
        if (left_length != left_match_array.size() || right_length != right_match_array.size()) {
            res->append_nulls(1);
            continue;
        }

        bool has_null_match_value = false;
        for (size_t i = 0; i < left_length; ++i) {
            if (left_match_array[i].is_null()) {
                has_null_match_value = true;
                break;
            }
        }
        for (size_t i = 0; i < right_length; ++i) {
            if (right_match_array[i].is_null()) {
                has_null_match_value = true;
                break;
            }
        }
        if (has_null_match_value) {
            res->append_nulls(1);
            continue;
        }
        std::optional<DatumArray> left_manual_array;
        if (!columns[4]->get(row).is_null()) {
            left_manual_array = columns[4]->get(row).get_array();
        }
        std::optional<DatumArray> right_manual_array;
        if (!columns[5]->get(row).is_null()) {
            right_manual_array = columns[5]->get(row).get_array();
        }
        if (left_manual_array.has_value()) {
            DCHECK(right_manual_array.has_value());
            if (left_manual_array->size() != right_manual_array->size()) {
                res->append_nulls(1);
                continue;
            }
            const auto manual_length = left_manual_array->size();
            bool has_null_manual_value = false;
            for (size_t i = 0; i < manual_length; ++i) {
                if (left_manual_array.value()[i].is_null()) {
                    has_null_manual_value = true;
                    break;
                }
            }
            for (size_t i = 0; i < manual_length; ++i) {
                if (right_manual_array.value()[i].is_null()) {
                    has_null_manual_value = true;
                    break;
                }
            }
            if (has_null_manual_value) {
                res->append_nulls(1);
                continue;
            }
        }

        if (fields[0]->is_nullable()) {
            auto null_column_1 = down_cast<NullableColumn*>(fields[0].get());
            null_column_1->null_column_data().emplace_back(0);
        }
        if (fields[1]->is_nullable()) {
            auto null_column_2 = down_cast<NullableColumn*>(fields[1].get());
            null_column_2->null_column_data().emplace_back(0);
        }
        std::vector<Edge> edges =
                compute_edges(left_match_array, right_match_array, build_map(left_manual_array, right_manual_array));
        AddEdges(edges, left_key_fields, right_key_fields, res_left_fields, res_right_fields, null_column, row);
    }
    return res;
}

StatusOr<ColumnPtr> CelonisTransitsMatch::transits_match_constant_manual(
        [[maybe_unused]] starrocks::FunctionContext* context, const starrocks::Columns& columns) {
    const size_t n_rows = columns[0]->size();
    auto& left_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto& right_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[2].get()))->fields();

    ColumnPtr res = context->create_column(context->get_return_type(), true);
    auto null_column = down_cast<NullableColumn*>(res.get());
    StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
    auto& fields = st->fields_column();
    DCHECK_EQ(2, fields.size());
    StructColumn* res_left_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    StructColumn* res_right_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    auto& res_left_fields = res_left_column->fields_column();
    auto& res_right_fields = res_right_column->fields_column();
    DCHECK(fields[0]->is_nullable());
    DCHECK(fields[1]->is_nullable());
    auto null_column_1 = down_cast<NullableColumn*>(fields[0].get());
    auto null_column_2 = down_cast<NullableColumn*>(fields[1].get());
    const auto* state = reinterpret_cast<const TransitsMatchStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    std::vector<ColumnPtr> res_left_elements;
    std::vector<ColumnPtr> res_right_elements;
    std::vector<NullableColumn*> res_left_nulls;
    std::vector<NullableColumn*> res_right_nulls;
    std::vector<UInt32Column::Ptr> res_left_offsets;
    std::vector<UInt32Column::Ptr> res_right_offsets;
    for (auto i = 0; i < res_left_fields.size(); ++i) {
        res_left_nulls.push_back(down_cast<NullableColumn*>(res_left_fields[i].get()));
        auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(res_left_fields[i].get()));
        res_left_elements.push_back(array_col->elements_column());
        res_left_offsets.push_back(array_col->offsets_column());
    }
    for (auto i = 0; i < res_right_fields.size(); ++i) {
        res_right_nulls.push_back(down_cast<NullableColumn*>(res_right_fields[i].get()));
        auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(res_right_fields[i].get()));
        res_right_elements.push_back(array_col->elements_column());
        res_right_offsets.push_back(array_col->offsets_column());
    }
    std::vector<const Column*> left_key_elements;
    std::vector<const Column*> right_key_elements;
    std::vector<UInt32Column::Ptr> left_key_offsets;
    std::vector<UInt32Column::Ptr> right_key_offsets;
    for (auto i = 0; i < left_key_fields.size(); ++i) {
        left_key_elements.push_back(
                down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(left_key_fields[i].get()))
                        ->elements_column()
                        .get());
        left_key_offsets.push_back(
                down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(left_key_fields[i].get()))
                        ->offsets_column());
    }
    for (auto i = 0; i < right_key_fields.size(); ++i) {
        right_key_elements.push_back(
                down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(right_key_fields[i].get()))
                        ->elements_column()
                        .get());
        right_key_offsets.push_back(
                down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(right_key_fields[i].get()))
                        ->offsets_column());
    }
    std::vector<uint32_t> left_indexes;
    std::vector<uint32_t> right_indexes;
    int new_offset = 0;
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row) ||
            columns[3]->is_null(row) || left_key_fields.size() == 0 || right_key_fields.size() == 0 ||
            state->is_malformed) {
            res->append_nulls(1);
            continue;
        }

        const auto left_length = left_key_offsets[0]->get_data()[row + 1] - left_key_offsets[0]->get_data()[row];
        const auto left_start = left_key_offsets[0]->get_data()[row];
        bool inconsistent_left_length = false;
        for (auto i = 1; i < left_key_fields.size(); ++i) {
            if (left_length != left_key_offsets[i]->get_data()[row + 1] - left_key_offsets[i]->get_data()[row]) {
                inconsistent_left_length = true;
                break;
            }
        }
        if (inconsistent_left_length) {
            res->append_nulls(1);
            continue;
        }

        const auto right_length = right_key_offsets[0]->get_data()[row + 1] - right_key_offsets[0]->get_data()[row];
        const auto right_start = right_key_offsets[0]->get_data()[row];
        bool inconsistent_right_length = false;
        for (auto i = 1; i < right_key_fields.size(); ++i) {
            if (right_length != right_key_offsets[i]->get_data()[row + 1] - right_key_offsets[i]->get_data()[row]) {
                inconsistent_right_length = true;
                break;
            }
        }
        if (inconsistent_right_length) {
            res->append_nulls(1);
            continue;
        }

        auto left_match_array = columns[1]->get(row).get_array();
        auto right_match_array = columns[3]->get(row).get_array();
        if (left_length != left_match_array.size() || right_length != right_match_array.size()) {
            res->append_nulls(1);
            continue;
        }

        bool has_null_match_value = false;
        for (size_t i = 0; i < left_length; ++i) {
            if (left_match_array[i].is_null()) {
                has_null_match_value = true;
                break;
            }
        }
        for (size_t i = 0; i < right_length; ++i) {
            if (right_match_array[i].is_null()) {
                has_null_match_value = true;
                break;
            }
        }
        if (has_null_match_value) {
            res->append_nulls(1);
            continue;
        }
        null_column_1->null_column_data().emplace_back(0);
        null_column_2->null_column_data().emplace_back(0);
        std::vector<Edge> edges = compute_edges(left_match_array, right_match_array, state->manual_map);
        for (const auto& edge : edges) {
            left_indexes.push_back(left_start + edge.left_index);
            right_indexes.push_back(right_start + edge.right_index);
        }
        new_offset += edges.size();
        for (auto i = 0; i < left_key_fields.size(); ++i) {
            res_left_offsets[i]->get_data().push_back(new_offset);
            res_left_nulls[i]->null_column_data().emplace_back(0);
        }
        for (auto i = 0; i < right_key_fields.size(); ++i) {
            res_right_offsets[i]->get_data().push_back(new_offset);
            res_right_nulls[i]->null_column_data().emplace_back(0);
        }
        null_column->null_column_data().emplace_back(0);
    }
    for (auto i = 0; i < left_key_fields.size(); ++i) {
        res_left_elements[i].get()->append_selective(*left_key_elements[i], left_indexes);
    }
    for (auto i = 0; i < right_key_fields.size(); ++i) {
        res_right_elements[i].get()->append_selective(*right_key_elements[i], right_indexes);
    }
    return res;
}

StatusOr<ColumnPtr> CelonisTransitsMatch::transits_match([[maybe_unused]] starrocks::FunctionContext* context,
                                                         const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 6);
    const auto* state = reinterpret_cast<const TransitsMatchStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
