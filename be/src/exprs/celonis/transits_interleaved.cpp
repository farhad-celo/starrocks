#include "exprs/celonis/transits_interleaved.h"

#include "column/array_column.h"
#include "column/column_viewer.h"
#include "column/struct_column.h"
#include "column/column_helper.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "util.h"

namespace starrocks {

namespace {

struct Node {
    bool from_left;
    size_t index;
};

struct Edge {
    Node start;
    Node end;
};

std::vector<Edge> keep_first_and_last(const std::vector<Edge>& edges) {
    std::vector<Edge> rv;
    if (edges.empty()) {
        return rv;
    }
    rv.push_back(edges[0]);
    for (auto it = edges.rbegin(); it != edges.rend(); ++it) {
        if (it->start.from_left != edges[0].start.from_left) {
            rv.push_back(*it);
            break;
        }
    }
    return rv;
}

}

StatusOr<ColumnPtr>
CelonisTransitsInterleaved::transits_interleaved([[maybe_unused]] starrocks::FunctionContext* context,
                                                 const starrocks::Columns& columns) {
    DCHECK_EQ(7, columns.size());
    const size_t n_rows = columns[0]->size();
    auto& left_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[0].get()))->fields();
    auto& right_key_fields = down_cast<const StructColumn*>(ColumnHelper::get_data_column(columns[3].get()))->fields();

    ColumnPtr left_timestamps_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[1]);
    UnnestedArrayData left_timestamps_data = prepare_array_input(left_timestamps_column.get());
    DCHECK(left_timestamps_data.elements->is_timestamp());
    const auto& left_timestamps =
            down_cast<const RunTimeColumnType<TYPE_DATETIME>&>(*left_timestamps_data.elements).get_data().data();
    const auto& left_timestamps_offsets = left_timestamps_data.offsets->get_data().data();
    const auto* left_timestamp_null_elements = left_timestamps_data.null_elements;

    ColumnPtr right_timestamps_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[4]);
    UnnestedArrayData right_timestamps_data = prepare_array_input(right_timestamps_column.get());
    DCHECK(right_timestamps_data.elements->is_timestamp());
    const auto& right_timestamps =
            down_cast<const RunTimeColumnType<TYPE_DATETIME>&>(*right_timestamps_data.elements).get_data().data();
    const auto& right_timestamps_offsets = right_timestamps_data.offsets->get_data().data();
    const auto* right_timestamp_null_elements = right_timestamps_data.null_elements;


    // Use sorting columns when both left_sortings and right_sortings are not NULL literal.
    const bool has_sorting_columns = (!columns[2]->has_null()) && (!columns[5]->has_null());
    ColumnPtr left_sortings_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[2]);
    ColumnPtr right_sortings_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[5]);
    const Column* left_sorting_elements = nullptr;
    const Column* right_sorting_elements = nullptr;
    const NullColumn::Container* left_sorting_null_elements = nullptr;
    const NullColumn::Container* right_sorting_null_elements = nullptr;
    if (has_sorting_columns) {
        UnnestedArrayData left_sortings_array_data = prepare_array_input(left_sortings_column.get());
        const auto& left_sortings_offsets = left_sortings_array_data.offsets->get_data().data();
        left_sorting_elements = left_sortings_array_data.elements;
        left_sorting_null_elements = left_sortings_array_data.null_elements;
        for (auto row = 0; row < n_rows; ++row) {
            const auto start = left_timestamps_offsets[row];
            const auto end = left_timestamps_offsets[row + 1];
            if (left_sortings_offsets[row] != start || left_sortings_offsets[row + 1] != end) {
                return Status::InvalidArgument(
                        "If provided, the size of left_sortings_array and left_timestamps_array should not be different.");
            }
        }
        UnnestedArrayData right_sortings_array_data = prepare_array_input(right_sortings_column.get());
        const auto& right_sortings_offsets = right_sortings_array_data.offsets->get_data().data();
        right_sorting_elements = right_sortings_array_data.elements;
        right_sorting_null_elements = right_sortings_array_data.null_elements;
        for (auto row = 0; row < n_rows; ++row) {
            const auto start = right_timestamps_offsets[row];
            const auto end = right_timestamps_offsets[row + 1];
            if (right_sortings_offsets[row] != start || right_sortings_offsets[row + 1] != end) {
                return Status::InvalidArgument(
                        "If provided, the size of right_sortings_array and right_timestamps_array should not be different.");
            }
        }
    }

    ColumnViewer first_last_only_viewer = ColumnViewer<TYPE_BOOLEAN>(columns[6]);
    ColumnPtr res = context->create_column(context->get_return_type(), true);
    auto null_column = down_cast<NullableColumn*>(res.get());
    StructColumn* st = down_cast<StructColumn*>(ColumnHelper::get_data_column(res.get()));
    auto& fields = st->fields_column();
    DCHECK_EQ(2, fields.size());
    StructColumn* res_left_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    StructColumn* res_right_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    auto& res_left_fields = res_left_column->fields_column();
    auto& res_right_fields = res_right_column->fields_column();

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
        left_key_elements.push_back(down_cast<const ArrayColumn*>(
                ColumnHelper::get_data_column(left_key_fields[i].get()))->elements_column().get());
        left_key_offsets.push_back(
                down_cast<const ArrayColumn*>(
                        ColumnHelper::get_data_column(left_key_fields[i].get()))->offsets_column());
    }
    for (auto i = 0; i < right_key_fields.size(); ++i) {
        right_key_elements.push_back(down_cast<const ArrayColumn*>(
                ColumnHelper::get_data_column(right_key_fields[i].get()))->elements_column().get());
        right_key_offsets.push_back(
                down_cast<const ArrayColumn*>(
                        ColumnHelper::get_data_column(right_key_fields[i].get()))->offsets_column());
    }
    std::vector<uint32_t> left_indexes;
    std::vector<uint32_t> right_indexes;
    left_indexes.reserve(n_rows);
    right_indexes.reserve(n_rows);
    int new_offset = 0;
    std::vector<Edge> edges;
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[3]->is_null(row) ||
            columns[4]->is_null(row) || columns[6]->is_null(row) || left_key_fields.size() == 0 ||
            right_key_fields.size() == 0) {
            res->append_nulls(1);
            continue;
        }
        const auto left_length = left_key_offsets[0]->get_data()[row + 1] - left_key_offsets[0]->get_data()[row];
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
        const auto left_start = left_timestamps_offsets[row];
        const auto left_end = left_timestamps_offsets[row + 1];
        const auto right_start = right_timestamps_offsets[row];
        const auto right_end = right_timestamps_offsets[row + 1];
        // The length of left_timestamps must match length of the struct elements of left_primary_keys.
        // The length of right_timestamps must match length of the struct elements of right_primary_keys.
        if (left_length != left_end - left_start || right_length != right_end - right_start) {
            res->append_nulls(1);
            continue;
        }

        bool has_null_timestamp = false;
        for (size_t i = left_start; i < left_end; ++i) {
            if (left_timestamp_null_elements != nullptr && (*left_timestamp_null_elements)[i]) {
                has_null_timestamp = true;
                break;
            }
        }
        for (size_t i = right_start; i < right_end; ++i) {
            if (right_timestamp_null_elements != nullptr && (*right_timestamp_null_elements)[i]) {
                has_null_timestamp = true;
                break;
            }
        }
        if (has_null_timestamp) {
            res->append_nulls(1);
            continue;
        }
        if (has_sorting_columns) {
            bool has_null_sorting = false;
            for (auto i = left_start; i < left_end; ++i) {
                if (left_sorting_null_elements != nullptr && (*left_sorting_null_elements)[i]) {
                    has_null_sorting = true;
                    break;
                }
            }
            for (auto i = right_start; i < right_end; ++i) {
                if (right_sorting_null_elements != nullptr && (*right_sorting_null_elements)[i]) {
                    has_null_sorting = true;
                    break;
                }
            }
            if (has_null_sorting) {
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
        std::optional<Node> pre_node = std::nullopt;
        size_t left_i = left_start;
        size_t right_i = right_start;
        edges.clear();
        while (left_i < left_end || right_i < right_end) {
            Node node = {false, 0};
            // Make sure left_primary_keys are ordered based on left_timestamps and left_sortings.
            if (left_i < left_end && left_i > left_start) {
                if (has_sorting_columns) {
                    DCHECK((left_timestamps[left_i - 1] < left_timestamps[left_i]) ||
                           (left_timestamps[left_i - 1] == left_timestamps[left_i] &&
                            !(left_sorting_elements->get(left_i - 1).convert2DatumKey() >
                              left_sorting_elements->get(left_i).convert2DatumKey())));
                } else {
                    DCHECK(left_timestamps[left_i - 1] <= left_timestamps[left_i]);
                }
            }
            // Make sure right_primary_keys are ordered based on right_timestamps and right_sortings.
            if (right_i < right_end && right_i > right_start) {
                if (has_sorting_columns) {
                    DCHECK((right_timestamps[right_i - 1] < right_timestamps[right_i]) ||
                           (right_timestamps[right_i - 1] == right_timestamps[right_i] &&
                            !(right_sorting_elements->get(right_i - 1).convert2DatumKey() >
                              right_sorting_elements->get(right_i).convert2DatumKey())));
                } else {
                    DCHECK(right_timestamps[right_i - 1] <= right_timestamps[right_i]);
                }
            }
            if (left_i < left_end && right_i < right_end) {
                if ((!has_sorting_columns &&
                     left_timestamps[left_i] <= right_timestamps[right_i]) ||
                    (has_sorting_columns &&
                     ((left_timestamps[left_i] < right_timestamps[right_i]) ||
                      (left_timestamps[left_i] == right_timestamps[right_i] &&
                       !(left_sorting_elements->get(left_i).convert2DatumKey() >
                         right_sorting_elements->get(right_i).convert2DatumKey()))))) {
                    node.from_left = true;
                    node.index = left_i++;
                } else {
                    node.from_left = false;
                    node.index = right_i++;
                }
            } else if (left_i < left_end) {
                node.from_left = true;
                node.index = left_i++;
            } else {
                node.from_left = false;
                node.index = right_i++;
            }
            if (pre_node.has_value() && pre_node->from_left != node.from_left) {
                edges.push_back(Edge{pre_node.value(), node});
            }
            pre_node = node;
        }
        const bool first_last_only = first_last_only_viewer.value(row);
        if (first_last_only) {
            edges = keep_first_and_last(edges);
        }
        for (const auto& edge: edges) {
            if (edge.start.from_left) {
                left_indexes.push_back(edge.start.index);
                right_indexes.push_back(edge.end.index);
            } else {
                left_indexes.push_back(edge.end.index);
                right_indexes.push_back(edge.start.index);
            }
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

} // namespace starrocks
