#include "exprs/celonis/encode_string.h"

#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/function_context.h"
#include "util/hash.h"
#include "util/phmap/phmap.h"

namespace starrocks {

namespace {

class Dict {
public:
    Dict() = default;
    explicit Dict(const DatumArray& arr) {
        for (const auto& entry : arr) {
            if (entry.is_null()) {
                continue;
            }

            const auto dict_key = _dict.size();
            _dict.try_emplace(entry.get_slice(), dict_key);
        }
    }

    [[nodiscard]] int32_t lookup(const Slice str) const {
        auto it = _dict.find(str);
        return it != _dict.end() ? it->second : -1;
    }

private:
    phmap::flat_hash_map<Slice, int32_t, SliceHashWithSeed<PhmapSeed1>, SliceEqual> _dict{};
};

struct EncodeStringStateFragmentLocal {
    Dict dict;
    bool null_map = false;
    ScalarFunction function;
};

} // namespace

Status CelonisEncodeString::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new EncodeStringStateFragmentLocal();
    context->set_function_state(scope, state);

    auto dict_column = context->get_constant_column(1);
    if (dict_column == nullptr) {
        state->function = encode_string_non_constant_map;
        // TODO(y.zhang): Consider turn on this.
        if (false && config::fail_query_when_expensive_non_const_impl_is_called) {
            return Status::InvalidArgument("The non-const version of CELONIS_ENCODE_STRING should not be called.");
        }
        return Status::OK();
    }
    state->function = encode_string_constant_map;

    if (dict_column->is_null(0)) {
        state->null_map = true;
        return Status::OK();
    }

    state->null_map = false;
    state->dict = Dict(dict_column->get(0).get_array());
    return Status::OK();
}

Status CelonisEncodeString::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const EncodeStringStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

StatusOr<ColumnPtr> CelonisEncodeString::encode_string_non_constant_map([[maybe_unused]] FunctionContext* context,
                                                                        const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    const ColumnViewer<TYPE_VARCHAR> string_viewer(columns[0]);
    const auto& dict_column = columns[1];
    ColumnBuilder<TYPE_INT> result(num_rows);

    for (size_t row = 0; row < num_rows; ++row) {
        if (string_viewer.is_null(row) || dict_column->is_null(row)) {
            result.append_null();
            continue;
        }

        // Construct a new dict for every row
        const Dict dict{dict_column->get(row).get_array()};
        result.append(dict.lookup(string_viewer.value(row)));
    }

    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisEncodeString::encode_string_constant_map(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(2, columns.size());
    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    const auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    const auto* state = reinterpret_cast<const EncodeStringStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    if (state->null_map) {
        return ColumnHelper::create_const_null_column(num_rows);
    }

    const ColumnViewer<TYPE_VARCHAR> string_viewer(columns[0]);
    ColumnBuilder<TYPE_INT> result(num_rows);

    for (size_t row = 0; row < num_rows; ++row) {
        if (string_viewer.is_null(row)) {
            result.append_null();
            continue;
        }

        result.append(state->dict.lookup(string_viewer.value(row)));
    }

    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisEncodeString::encode_string(FunctionContext* context, const Columns& columns) {
    const auto* state = reinterpret_cast<const EncodeStringStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

} // namespace starrocks
