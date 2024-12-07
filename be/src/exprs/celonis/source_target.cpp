#include "exprs/celonis/source_target.h"

#include <map>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

template<bool is_source, bool has_group, bool has_null_element, bool has_null_group_element>
ColumnPtr CelonisSourceTargetFunctions::_celonis_array_sources_targets_impl(
        const UnnestedArrayData& array_data, const UnnestedArrayData& group_array_data) {
    const size_t num_array = array_data.offsets->size() - 1;
    auto offsets_ptr = array_data.offsets->get_data().data();
    const UInt32Column::Container::value_type* group_offsets_ptr = nullptr;
    const RunTimeCppType<TYPE_BIGINT>* group_elements = nullptr;
    if constexpr (has_group) {
        group_offsets_ptr = group_array_data.offsets->get_data().data();
        group_elements =
                down_cast<const RunTimeColumnType<TYPE_BIGINT>*>(group_array_data.elements)->get_data().data();
    }
    auto result_array = ArrayColumn::create(
            NullableColumn::create(array_data.elements->clone_empty(), NullColumn::create()), UInt32Column::create());
    UInt32Column::Container& result_offsets = result_array->offsets_column()->get_data();
    ColumnPtr& result_elements = result_array->elements_column();

    result_offsets.reserve(num_array);
    result_elements->reserve(array_data.elements->size());
    size_t new_offset = 0;
    for (size_t i = 0; i < num_array; i++) {
        size_t offset = offsets_ptr[i];
        size_t array_size = offsets_ptr[i + 1] - offset;
        if ((array_data.null_arrays != nullptr && (*array_data.null_arrays)[i]) || array_size == 0) {
            result_offsets.push_back(new_offset);
            continue;
        }
        if constexpr (has_group) {
            size_t group_offset = group_offsets_ptr[i];
            if (array_size != group_offsets_ptr[i + 1] - group_offset) {
                result_offsets.push_back(new_offset);
                continue;
            }
            std::map<int64_t, std::vector<uint32_t>> index_map;  // Ordered map
            if constexpr (has_null_group_element) {
                bool group_has_null = false;
                for (int j = 0; j < array_size; j++) {
                    if ((*group_array_data.null_elements)[group_offset + j] != 0) {
                        group_has_null = true;
                        break;
                    }
                    index_map[group_elements[group_offset + j]].push_back(offset + j);
                }
                if (group_has_null) {
                    result_offsets.push_back(new_offset);
                    continue;
                }
            } else {
                for (int j = 0; j < array_size; j++) {
                    index_map[group_elements[group_offset + j]].push_back(offset + j);
                }
            }
            for (auto it: index_map) {
                auto &index = it.second;
                if constexpr (is_source) {
                    index.pop_back();
                } else {
                    index.erase(index.begin());
                }
                if (index.empty()) continue;
                result_elements->append_selective(*array_data.elements, index);
                if constexpr (has_null_element) {
                    for (int j = 0; j < index.size(); j++) {
                        if ((*array_data.null_elements)[index[j]] != 0) {
                            auto res = (result_elements->set_null(new_offset + j));
                            DCHECK(res);
                        }
                    }
                }
                new_offset += index.size();
            }
        } else {
            if constexpr (!is_source) {
                offset++;
            }
            result_elements->append(*array_data.elements, offset, array_size - 1);
            if constexpr (has_null_element) {
                // Input has nulls, propagate them to output.
                for (int j = 0; j < array_size - 1; ++j) {
                    if ((*array_data.null_elements)[offset + j] != 0) {
                        auto res = (result_elements->set_null(new_offset + j));
                        DCHECK(res);
                    }
                }
            }
            new_offset += array_size - 1;
        }
        result_offsets.push_back(new_offset);
    }
    return result_array;
}

template<bool is_source>
ColumnPtr CelonisSourceTargetFunctions::_celonis_array_sources_targets_impl(
        const UnnestedArrayData& array_data, const UnnestedArrayData& group_array_data) {
    if (group_array_data.elements != nullptr) {
        if (array_data.null_elements != nullptr) {
            if (group_array_data.null_elements != nullptr) {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/true, /*has_null_element=*/true, /*has_null_group_element=*/true>
                        (array_data, group_array_data);
            } else {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/true, /*has_null_element=*/true, /*has_null_group_element=*/false>
                        (array_data, group_array_data);
            }
        } else {
            if (group_array_data.null_elements != nullptr) {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/true, /*has_null_element=*/false, /*has_null_group_element=*/true>
                        (array_data, group_array_data);
            } else {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/true, /*has_null_element=*/false, /*has_null_group_element=*/false>
                        (array_data, group_array_data);
            }
        }
    } else {
        if (array_data.null_elements != nullptr) {
            if (group_array_data.null_elements != nullptr) {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/false, /*has_null_element=*/true, /*has_null_group_element=*/true>
                        (array_data, group_array_data);
            } else {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/false, /*has_null_element=*/true, /*has_null_group_element=*/false>
                        (array_data, group_array_data);
            }
        } else {
            if (group_array_data.null_elements != nullptr) {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/false, /*has_null_element=*/false, /*has_null_group_element=*/true>
                        (array_data, group_array_data);
            } else {
                return _celonis_array_sources_targets_impl
                        <is_source, /*has_group=*/false, /*has_null_element=*/false, /*has_null_group_element=*/false>
                        (array_data, group_array_data);
            }
        }
    }
}

static CelonisSourceTargetFunctions::EdgeConfig getEdgeConfig(const std::string& format) {
    if (format == "any->any") {
        return CelonisSourceTargetFunctions::ANY_TO_ANY;
    }
    return CelonisSourceTargetFunctions::DEFAULT;
}

Status CelonisSourceTargetFunctions::celonis_array_sources_prepare(starrocks::FunctionContext *context,
                                                                   FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::InvalidArgument(
                    "The second parameter of celonis_array_sources() only accepts a literal value");
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        auto edge_config_column = context->get_constant_column(1);
        CelonisSourceTargetFunctions::EdgeConfig edge_config = getEdgeConfig(
                ColumnHelper::get_const_value<TYPE_VARCHAR>(edge_config_column).to_string());
        if (edge_config != ANY_TO_ANY) {
            // TODO(j.kim): support other edge configurations.
            return Status::InvalidArgument("unsupported edge configuration in celonis_array_targets()");
        }
    }

    return Status::OK();
}

Status CelonisSourceTargetFunctions::celonis_array_sources_close(starrocks::FunctionContext *context,
                                                                 FunctionContext::FunctionStateScope scope) {
    return Status::OK();
}

Status CelonisSourceTargetFunctions::celonis_array_targets_prepare(starrocks::FunctionContext *context,
                                                                   FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            return Status::InvalidArgument(
                    "The second parameter of celonis_array_targets() only accepts a literal value");
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        auto edge_config_column = context->get_constant_column(1);
        CelonisSourceTargetFunctions::EdgeConfig edge_config = getEdgeConfig(
                ColumnHelper::get_const_value<TYPE_VARCHAR>(edge_config_column).to_string());
        if (edge_config != ANY_TO_ANY) {
            // TODO(j.kim): support other edge configurations.
            return Status::InvalidArgument("unsupported edge configuration in celonis_array_targets()");
        }
    }

    return Status::OK();
}

Status CelonisSourceTargetFunctions::celonis_array_targets_close(starrocks::FunctionContext *context,
                                                                 FunctionContext::FunctionStateScope scope) {
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisSourceTargetFunctions::celonis_array_sources(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    RETURN_IF_COLUMNS_ONLY_NULL({columns[1]});

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    ColumnPtr group_column;
    UnnestedArrayData group_array_data;
    if (columns.size() == 3) {
        if (columns[2]->only_null()) {
            // If columns[2] is only NULL, SR passes not an array column with null but a const null column. We simply
            // ignore the column instead of trying returning empty arrays which is the behavior when a single array of
            // columns[2] is NULL.
        } else {
            group_column = ColumnHelper::unpack_and_duplicate_const_column(columns[2]->size(), columns[2]);
            group_array_data = prepare_array_input(group_column.get());
        }
    }

    auto result = _celonis_array_sources_targets_impl</*is_source=*/true>(array_data, group_array_data);
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result), down_cast<const NullableColumn *>(array_column.get())->null_column());
    }
    return result;
}

StatusOr<ColumnPtr> CelonisSourceTargetFunctions::celonis_array_targets(FunctionContext* context, const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    RETURN_IF_COLUMNS_ONLY_NULL({columns[1]});

    ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    UnnestedArrayData array_data = prepare_array_input(array_column.get());
    ColumnPtr group_column;
    UnnestedArrayData group_array_data;
    if (columns.size() == 3) {
        if (columns[2]->only_null()) {
            // If columns[2] is only NULL, SR passes not an array column with null but a const null column. We simply
            // ignore the column instead of trying returning empty arrays which is the behavior when a single array of
            // columns[2] is NULL.
        } else {
            group_column = ColumnHelper::unpack_and_duplicate_const_column(columns[2]->size(), columns[2]);
            group_array_data = prepare_array_input(group_column.get());
        }
    }

    auto result = _celonis_array_sources_targets_impl</*is_source=*/false>(array_data, group_array_data);
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result), down_cast<const NullableColumn *>(array_column.get())->null_column());
    }
    return result;
}

} // namespace starrocks
