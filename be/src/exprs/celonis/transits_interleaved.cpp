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

void AppendFields(const Columns& source_fields, std::vector<DatumArray>& arrays, size_t row, size_t index) {
    const auto n_fields = source_fields.size();
    for (auto i = 0; i < n_fields; ++i) {
        arrays[i].push_back(source_fields[i]->get(row).get_array()[index]);
    }
}

void
AppendNode(const Node& node, const Columns& left_key_fields, const Columns& right_key_fields,
           std::vector<DatumArray>& arrays, size_t row) {
    if (node.from_left) {
        AppendFields(left_key_fields, arrays, row, node.index);
    } else {
        AppendFields(right_key_fields, arrays, row, node.index);
    }
}

void
AddEdges(const std::vector<Edge>& edges, const Columns& left_key_fields, const Columns& right_key_fields,
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
    for (const auto& edge: edges) {
        if (edge.start.from_left) {
            DCHECK(!edge.end.from_left);
            AppendNode(edge.start, left_key_fields, right_key_fields, left_arrays, row);
            AppendNode(edge.end, left_key_fields, right_key_fields, right_arrays, row);
        } else {
            DCHECK(edge.end.from_left);
            AppendNode(edge.end, left_key_fields, right_key_fields, left_arrays, row);
            AppendNode(edge.start, left_key_fields, right_key_fields, right_arrays, row);
        }
    }
    null_column->null_column_data().emplace_back(0);
    for (auto i = 0; i < n_left_fields; ++i) {
        res_left_fields[i]->append_datum(left_arrays[i]);
    }
    for (auto i = 0; i < n_right_fields; ++i) {
        res_right_fields[i]->append_datum(right_arrays[i]);
    }
}

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

    ColumnPtr right_timestamps_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[4]);
    UnnestedArrayData right_timestamps_data = prepare_array_input(right_timestamps_column.get());
    DCHECK(right_timestamps_data.elements->is_timestamp());
    const auto& right_timestamps =
            down_cast<const RunTimeColumnType<TYPE_DATETIME>&>(*right_timestamps_data.elements).get_data().data();
    const auto& right_timestamps_offsets = right_timestamps_data.offsets->get_data().data();

    // Use sorting columns when both left_sortings and right_sortings are not NULL literal.
    const bool has_sorting_columns = (!columns[2]->has_null()) && (!columns[5]->has_null());
    ColumnPtr left_sortings_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[2]);
    ColumnPtr right_sortings_column = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[5]);
    if (has_sorting_columns) {
        UnnestedArrayData left_sortings_array_data = prepare_array_input(left_sortings_column.get());
        const auto& left_sortings_offsets = left_sortings_array_data.offsets->get_data().data();
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
    auto fields = st->fields_column();
    DCHECK_EQ(2, fields.size());
    StructColumn* res_left_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[0].get()));
    StructColumn* res_right_column = down_cast<StructColumn*>(ColumnHelper::get_data_column(fields[1].get()));
    auto res_left_fields = res_left_column->fields_column();
    auto res_right_fields = res_right_column->fields_column();
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[3]->is_null(row) ||
            columns[4]->is_null(row) || columns[6]->is_null(row) || left_key_fields.size() == 0 ||
            right_key_fields.size() == 0) {
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
            if (left_timestamps_data.null_elements != nullptr && (*left_timestamps_data.null_elements)[i]) {
                has_null_timestamp = true;
                break;
            }
        }
        for (size_t i = right_start; i < right_end; ++i) {
            if (right_timestamps_data.null_elements != nullptr && (*right_timestamps_data.null_elements)[i]) {
                has_null_timestamp = true;
                break;
            }
        }
        if (has_null_timestamp) {
            res->append_nulls(1);
            continue;
        }
        DatumArray left_sortings;
        DatumArray right_sortings;
        if (has_sorting_columns) {
            left_sortings = left_sortings_column->get(row).get_array();
            right_sortings = right_sortings_column->get(row).get_array();
            bool has_null_sorting = false;
            for (const auto& item: left_sortings) {
                if (item.is_null()) {
                    has_null_sorting = true;
                    break;
                }
            }
            for (const auto& item: right_sortings) {
                if (item.is_null()) {
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
        std::vector<Edge> edges;
        std::optional<Node> pre_node = std::nullopt;
        size_t left_i = 0;
        size_t right_i = 0;
        while (left_i < left_length || right_i < right_length) {
            Node node = {false, 0};
            // Make sure left_primary_keys are ordered based on left_timestamps and left_sortings.
            if (left_i < left_length && left_i > 0) {
                if (has_sorting_columns) {
                    DCHECK((left_timestamps[left_i - 1 + left_start] < left_timestamps[left_i + left_start]) ||
                           (left_timestamps[left_i - 1 + left_start] == left_timestamps[left_i + left_start] &&
                            !(left_sortings[left_i - 1].convert2DatumKey() >
                              left_sortings[left_i].convert2DatumKey())));
                } else {
                    DCHECK(left_timestamps[left_i - 1 + left_start] <= left_timestamps[left_i + left_start]);
                }
            }
            // Make sure right_primary_keys are ordered based on right_timestamps and right_sortings.
            if (right_i < right_length && right_i > 0) {
                if (has_sorting_columns) {
                    DCHECK((right_timestamps[right_i - 1 + right_start] < right_timestamps[right_i + right_start]) ||
                           (right_timestamps[right_i - 1 + right_start] == right_timestamps[right_i + right_start] &&
                            !(right_sortings[right_i - 1].convert2DatumKey() >
                              right_sortings[right_i].convert2DatumKey())));
                } else {
                    DCHECK(right_timestamps[right_i - 1 + right_start] <= right_timestamps[right_i + right_start]);
                }
            }
            if (left_i < left_length && right_i < right_length) {
                if ((!has_sorting_columns &&
                     left_timestamps[left_i + left_start] <= right_timestamps[right_i + right_start]) ||
                    (has_sorting_columns &&
                     ((left_timestamps[left_i + left_start] < right_timestamps[right_i + right_start]) ||
                      (left_timestamps[left_i + left_start] == right_timestamps[right_i + right_start] &&
                       !(left_sortings[left_i].convert2DatumKey() > right_sortings[right_i].convert2DatumKey()))))) {
                    node.from_left = true;
                    node.index = left_i++;
                } else {
                    node.from_left = false;
                    node.index = right_i++;
                }
            } else if (left_i < left_length) {
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
            AddEdges(keep_first_and_last(edges), left_key_fields, right_key_fields, res_left_fields, res_right_fields,
                     null_column, row);
        } else {
            AddEdges(edges, left_key_fields, right_key_fields, res_left_fields, res_right_fields, null_column, row);
        }
    }
    return res;
}

} // namespace starrocks
