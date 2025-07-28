#include "exprs/celonis/source_target.h"

#include <exprs/builtin_functions.h>

#include <map>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

struct SourceTargetStateFragmentLocal {
    ScalarFunction function;
};

template <bool has_null_element, bool is_first_or_last>
void handle_null_elements(ColumnPtr& result_elements, const size_t new_offset, const size_t offset,
                          const UnnestedArrayData& array_data, const size_t array_size) {
    if constexpr (has_null_element) {
        if constexpr (is_first_or_last) {
            if ((*array_data.null_elements)[offset] != 0) {
                for (int j = 0; j < array_size; ++j) {
                    const auto res{(result_elements->set_null(new_offset + j))};
                    DCHECK(res);
                }
            }
        } else {
            for (int j = 0; j < array_size; ++j) {
                if ((*array_data.null_elements)[offset + j] != 0) {
                    const auto res{(result_elements->set_null(new_offset + j))};
                    DCHECK(res);
                }
            }
        }
    }
}

template <bool is_source, bool has_null_element>
void execute_any_to_any(ColumnPtr& result_elements, size_t& new_offset, size_t offset,
                        const UnnestedArrayData& array_data, const size_t array_size) {
    if constexpr (!is_source) {
        offset++;
    }
    result_elements->append(*array_data.elements, offset, array_size - 1);
    handle_null_elements<has_null_element, /* is_first_or_last */ false>(result_elements, new_offset, offset,
                                                                         array_data, array_size - 1);
    new_offset += array_size - 1;
}

template <bool is_source, bool has_null_element>
void execute_first_to_any(ColumnPtr& result_elements, size_t& new_offset, size_t offset,
                          const UnnestedArrayData& array_data, const size_t array_size) {
    if constexpr (!is_source) {
        offset++;
        result_elements->append(*array_data.elements, offset, array_size - 1);
        handle_null_elements<has_null_element, /* is_first_or_last */ false>(result_elements, new_offset, offset,
                                                                             array_data, array_size - 1);
    } else {
        result_elements->append_value_multiple_times(*array_data.elements, offset, array_size - 1);
        handle_null_elements<has_null_element, /* is_first_or_last */ true>(result_elements, new_offset, offset,
                                                                            array_data, array_size - 1);
    }
    new_offset += array_size - 1;
}

template <bool is_source, bool has_null_element>
void execute_first_to_any_with_self(ColumnPtr& result_elements, size_t& new_offset, const size_t offset,
                                    const UnnestedArrayData& array_data, const size_t array_size) {
    if constexpr (!is_source) {
        result_elements->append(*array_data.elements, offset, array_size);
        handle_null_elements<has_null_element, /* is_first_or_last */ false>(result_elements, new_offset, offset,
                                                                             array_data, array_size);
    } else {
        result_elements->append_value_multiple_times(*array_data.elements, offset, array_size);
        handle_null_elements<has_null_element, /* is_first_or_last */ true>(result_elements, new_offset, offset,
                                                                            array_data, array_size);
    }
    new_offset += array_size;
}

template <bool is_source, bool has_null_element>
void execute_any_to_last(ColumnPtr& result_elements, size_t& new_offset, size_t offset,
                         const UnnestedArrayData& array_data, const size_t array_size) {
    if constexpr (!is_source) {
        offset = offset + array_size - 1;
        result_elements->append_value_multiple_times(*array_data.elements, offset, array_size - 1);
        handle_null_elements<has_null_element, /* is_first_or_last */ true>(result_elements, new_offset, offset,
                                                                            array_data, array_size - 1);
    } else {
        result_elements->append(*array_data.elements, offset, array_size - 1);
        handle_null_elements<has_null_element, /* is_first_or_last */ false>(result_elements, new_offset, offset,
                                                                             array_data, array_size - 1);
    }
    new_offset += array_size - 1;
}

template <bool is_source, bool has_null_element>
void execute_first_to_last(ColumnPtr& result_elements, size_t& new_offset, size_t offset,
                           const UnnestedArrayData& array_data, const size_t array_size) {
    if (array_size < 2) {
        return;
    }
    if constexpr (!is_source) {
        offset = offset + array_size - 1;
    }
    result_elements->append(*array_data.elements, offset, 1);
    if constexpr (has_null_element) {
        if ((*array_data.null_elements)[offset] != 0) {
            const auto res{(result_elements->set_null(new_offset))};
            DCHECK(res);
        }
    }
    new_offset += 1;
}

template <bool is_source, SourceTargetEdgeConfig edge_config, bool has_group, bool has_null_element,
          bool has_null_group_element>
ColumnPtr array_sources_targets_impl(const UnnestedArrayData& array_data, const UnnestedArrayData& group_array_data) {
    const size_t num_array = array_data.offsets->size() - 1;
    auto offsets_ptr = array_data.offsets->get_data().data();
    const UInt32Column::Container::value_type* group_offsets_ptr = nullptr;
    const RunTimeCppType<TYPE_BIGINT>* group_elements = nullptr;
    if constexpr (has_group) {
        group_offsets_ptr = group_array_data.offsets->get_data().data();
        group_elements = down_cast<const RunTimeColumnType<TYPE_BIGINT>*>(group_array_data.elements)->get_data().data();
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
            std::map<int64_t, std::vector<uint32_t>> index_map; // Ordered map
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
            for (auto it : index_map) {
                auto& index = it.second;
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
            if constexpr (edge_config == SourceTargetEdgeConfig::ANY_TO_ANY) {
                execute_any_to_any<is_source, has_null_element>(result_elements, new_offset, offset, array_data,
                                                                array_size);
            }
            if constexpr (edge_config == SourceTargetEdgeConfig::FIRST_TO_ANY) {
                execute_first_to_any<is_source, has_null_element>(result_elements, new_offset, offset, array_data,
                                                                  array_size);
            }
            if constexpr (edge_config == SourceTargetEdgeConfig::FIRST_TO_ANY_WITH_SELF) {
                execute_first_to_any_with_self<is_source, has_null_element>(result_elements, new_offset, offset,
                                                                            array_data, array_size);
            }
            if constexpr (edge_config == SourceTargetEdgeConfig::ANY_TO_LAST) {
                execute_any_to_last<is_source, has_null_element>(result_elements, new_offset, offset, array_data,
                                                                 array_size);
            }
            if constexpr (edge_config == SourceTargetEdgeConfig::FIRST_TO_LAST) {
                execute_first_to_last<is_source, has_null_element>(result_elements, new_offset, offset, array_data,
                                                                   array_size);
            }
        }
        result_offsets.push_back(new_offset);
    }
    return result_array;
}

template <bool is_source, SourceTargetEdgeConfig edge_config>
ColumnPtr array_sources_targets_impl(const UnnestedArrayData& array_data, const UnnestedArrayData& group_array_data) {
    if (group_array_data.elements != nullptr) {
        if (array_data.null_elements != nullptr) {
            if (group_array_data.null_elements != nullptr) {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/true,
                                                  /*has_null_element=*/true,
                                                  /*has_null_group_element=*/true>(array_data, group_array_data);
            } else {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/true,
                                                  /*has_null_element=*/true,
                                                  /*has_null_group_element=*/false>(array_data, group_array_data);
            }
        } else {
            if (group_array_data.null_elements != nullptr) {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/true,
                                                  /*has_null_element=*/false,
                                                  /*has_null_group_element=*/true>(array_data, group_array_data);
            } else {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/true,
                                                  /*has_null_element=*/false,
                                                  /*has_null_group_element=*/false>(array_data, group_array_data);
            }
        }
    } else {
        if (array_data.null_elements != nullptr) {
            if (group_array_data.null_elements != nullptr) {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/false,
                                                  /*has_null_element=*/true,
                                                  /*has_null_group_element=*/true>(array_data, group_array_data);
            } else {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/false,
                                                  /*has_null_element=*/true,
                                                  /*has_null_group_element=*/false>(array_data, group_array_data);
            }
        } else {
            if (group_array_data.null_elements != nullptr) {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/false,
                                                  /*has_null_element=*/false,
                                                  /*has_null_group_element=*/true>(array_data, group_array_data);
            } else {
                return array_sources_targets_impl<is_source, edge_config, /*has_group=*/false,
                                                  /*has_null_element=*/false,
                                                  /*has_null_group_element=*/false>(array_data, group_array_data);
            }
        }
    }
}

template <SourceTargetType SOURCE_TARGET_TYPE, SourceTargetEdgeConfig EDGE_CONFIG>
StatusOr<ColumnPtr> array_sources_targets_impl(FunctionContext* context, const Columns& columns) {
    const auto array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[0]->size(), columns[0]);
    const auto array_data{prepare_array_input(array_column.get())};
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

    auto result = array_sources_targets_impl< /*is_source=*/SOURCE_TARGET_TYPE == SourceTargetType::SOURCE,
            EDGE_CONFIG>(array_data, group_array_data);
    if (array_data.null_arrays != nullptr) {
        return NullableColumn::create(std::move(result),
                                      down_cast<const NullableColumn*>(array_column.get())->null_column());
    }
    return result;
}

SourceTargetEdgeConfig getEdgeConfig(const std::string& format) {
    if (format == "any->any") {
        return SourceTargetEdgeConfig::ANY_TO_ANY;
    }
    if (format == "first->any") {
        return SourceTargetEdgeConfig::FIRST_TO_ANY;
    }
    if (format == "first->any_with_self") {
        return SourceTargetEdgeConfig::FIRST_TO_ANY_WITH_SELF;
    }
    if (format == "any->last") {
        return SourceTargetEdgeConfig::ANY_TO_LAST;
    }
    if (format == "first->last") {
        return SourceTargetEdgeConfig::FIRST_TO_LAST;
    }
    return SourceTargetEdgeConfig::DEFAULT;
}

} // namespace

template <SourceTargetType SOURCE_TARGET_TYPE>
StatusOr<ColumnPtr> CelonisSourceTarget<SOURCE_TARGET_TYPE>::array_sources_targets(FunctionContext* context,
                                                                                   const Columns& columns) {
    RETURN_IF_COLUMNS_ONLY_NULL({columns[0]});
    RETURN_IF_COLUMNS_ONLY_NULL({columns[1]});
    const auto* state{reinterpret_cast<const SourceTargetStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL))};
    return state->function(context, columns);
}

template <SourceTargetType SOURCE_TARGET_TYPE>
Status CelonisSourceTarget<SOURCE_TARGET_TYPE>::array_sources_targets_prepare(
        FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        if (!context->is_constant_column(1)) {
            constexpr auto function_name{SOURCE_TARGET_TYPE == SourceTargetType::SOURCE ? "celonis_array_sources()"
                                                                                        : "celonis_array_targets()"};
            return Status::InvalidArgument(
                    fmt::format("The second parameter of {} only accepts a literal value", function_name));
        }
        if (!context->is_notnull_constant_column(1)) {
            return Status::OK();
        }
        const auto edge_config_column = context->get_constant_column(1);
        const auto edge_config{
                getEdgeConfig(ColumnHelper::get_const_value<TYPE_VARCHAR>(edge_config_column).to_string())};

        const auto state{new SourceTargetStateFragmentLocal{}};
        context->set_function_state(scope, state);

        switch (edge_config) {
        case SourceTargetEdgeConfig::ANY_TO_ANY:
            state->function = array_sources_targets_impl<SOURCE_TARGET_TYPE, SourceTargetEdgeConfig::ANY_TO_ANY>;
            break;
        case SourceTargetEdgeConfig::FIRST_TO_ANY:
            state->function = array_sources_targets_impl<SOURCE_TARGET_TYPE, SourceTargetEdgeConfig::FIRST_TO_ANY>;
            break;
        case SourceTargetEdgeConfig::FIRST_TO_ANY_WITH_SELF:
            state->function =
                    array_sources_targets_impl<SOURCE_TARGET_TYPE, SourceTargetEdgeConfig::FIRST_TO_ANY_WITH_SELF>;
            break;
        case SourceTargetEdgeConfig::ANY_TO_LAST:
            state->function = array_sources_targets_impl<SOURCE_TARGET_TYPE, SourceTargetEdgeConfig::ANY_TO_LAST>;
            break;
        case SourceTargetEdgeConfig::FIRST_TO_LAST:
            state->function = array_sources_targets_impl<SOURCE_TARGET_TYPE, SourceTargetEdgeConfig::FIRST_TO_LAST>;
            break;
        default:
            constexpr auto function_name{SOURCE_TARGET_TYPE == SourceTargetType::SOURCE ? "celonis_array_sources()"
                                                                                        : "celonis_array_targets()"};
            return Status::InvalidArgument(fmt::format("unsupported edge configuration in {}", function_name));
        }
    }

    return Status::OK();
}

template <SourceTargetType SOURCE_TARGET_TYPE>
Status CelonisSourceTarget<SOURCE_TARGET_TYPE>::array_sources_targets_close(FunctionContext* context,
                                                                            FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const SourceTargetStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

template class CelonisSourceTarget<SourceTargetType::SOURCE>;

template class CelonisSourceTarget<SourceTargetType::TARGET>;

} // namespace starrocks
