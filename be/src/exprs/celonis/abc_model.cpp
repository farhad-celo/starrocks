#include "exprs/celonis/abc_model.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "column/column_builder.h"
#include "column/column_hash.h"
#include "column/column_viewer.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/function_context.h"

namespace starrocks {

namespace {

static const double EPS = 1e-9;

template <LogicalType LT>
class AbcModel {
    using CppType = RunTimeCppType<LT>;

public:
    int64_t label(CppType x, int64_t pk_hash) const {
        for (int label = 1; label <= 3; ++label) {
            CppType low = ranges_[label].first;
            CppType high = ranges_[label].second;
            if (low <= x && x <= high) {
                auto it = num_to_probs_.find(x);
                if (it == num_to_probs_.end()) {
                    return label;
                } else {
                    double prob = static_cast<double>(safe_abs(pk_hash)) /
                                  static_cast<double>(std::numeric_limits<int64_t>::max());
                    if (prob < it->second[1]) {
                        return 1;
                    } else if (prob < it->second[2]) {
                        return 2;
                    } else {
                        return 3;
                    }
                }
            }
        }
        return -1;
    }

    static std::optional<AbcModel> create_model(const std::string& model) {
        std::vector<std::string> parts;
        boost::split(parts, model, boost::is_any_of(":"));
        if (parts.size() != 2) {
            return std::nullopt;
        }
        std::vector<std::string> boundary_strs;
        boost::split(boundary_strs, parts[0], boost::is_any_of(","));
        if (boundary_strs.size() != 6) {
            return std::nullopt;
        }
        std::vector<CppType> boundaries;
        for (const auto& boundary_str : boundary_strs) {
            try {
                auto boundary = boost::lexical_cast<CppType>(boundary_str);
                boundaries.push_back(boundary);
            } catch (const boost::bad_lexical_cast& e) {
                return std::nullopt;
            }
        }
        std::vector<std::pair<CppType, CppType>> ranges;
        ranges.resize(4);
        ranges[1] = std::make_pair(boundaries[0], boundaries[1]);
        ranges[2] = std::make_pair(boundaries[2], boundaries[3]);
        ranges[3] = std::make_pair(boundaries[4], boundaries[5]);
        phmap::flat_hash_map<CppType, std::vector<double>, StdHash<CppType>> num_to_probs;
        if (!parts[1].empty()) {
            std::vector<std::string> num_section_strs;
            boost::split(num_section_strs, parts[1], boost::is_any_of(";"));
            for (const auto& num_section_str : num_section_strs) {
                std::vector<std::string> value_strs;
                boost::split(value_strs, num_section_str, boost::is_any_of(","));
                if (value_strs.size() != 4) {
                    return std::nullopt;
                }
                std::vector<double> probs;
                CppType num;
                for (int i = 0; i < 4; ++i) {
                    try {
                        if (i == 0) {
                            num = boost::lexical_cast<CppType>(value_strs[0]);
                        } else {
                            auto value = boost::lexical_cast<double>(value_strs[i]);
                            probs.push_back(value);
                        }
                    } catch (const boost::bad_lexical_cast& e) {
                        return std::nullopt;
                    }
                }
                double prob1 = probs[0];
                double prob2 = probs[1];
                double prob3 = probs[2];
                if (prob1 < -EPS || prob1 > 1.0 + EPS || prob2 < -EPS || prob2 > 1.0 + EPS || prob3 < -EPS ||
                    prob3 > 1.0 + EPS) {
                    return std::nullopt;
                }
                num_to_probs[num] = {0.0, prob1, prob1 + prob2};
            }
        }
        return AbcModel<LT>(ranges, num_to_probs);
    }

private:
    AbcModel(const std::vector<std::pair<CppType, CppType>>& ranges,
             const phmap::flat_hash_map<CppType, std::vector<double>, StdHash<CppType>>& num_to_probs)
            : ranges_(ranges), num_to_probs_(num_to_probs) {}

    // [{0, 0}, {lo_1, hi_1}, {lo_2, hi_2}, {lo_3, hi_3}]
    std::vector<std::pair<CppType, CppType>> ranges_;
    // {num: [0.0, prob_1, prob_1 + prob_2]}
    phmap::flat_hash_map<CppType, std::vector<double>, StdHash<CppType>> num_to_probs_;
};

} // namespace

template <LogicalType LT>
struct AbcModelStateFragmentLocal {
    AbcModelStateFragmentLocal() : model(std::nullopt) {}

    std::optional<AbcModel<LT>> model;
    ScalarFunction function;
};

template <LogicalType LT>
Status CelonisAbcModel<LT>::prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }

    auto state = new AbcModelStateFragmentLocal<LT>();
    context->set_function_state(scope, state);

    auto model_column = context->get_constant_column(2);
    // context->is_constant_column(i) must not be used to determine if the argument is Array Literal because as of
    // 2024-02-29 it returns false for Array Literal while get_constant_column(i) returns non nullptr.
    // For the same reason, columns[i]->is_constant() must not be used in celonis_apply_abc_model().
    if (model_column == nullptr) {
        state->function = apply_abc_model_non_constant_model;
        return Status::OK();
    }
    state->function = apply_abc_model_constant_model;

    if (model_column->is_null(0)) {
        return Status::OK();
    }

    state->model = AbcModel<LT>::create_model(model_column->get(0).get_slice().to_string());
    return Status::OK();
}

template <LogicalType LT>
Status CelonisAbcModel<LT>::close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const AbcModelStateFragmentLocal<LT>*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

template <LogicalType LT>
StatusOr<ColumnPtr> CelonisAbcModel<LT>::apply_abc_model_non_constant_model([[maybe_unused]] FunctionContext* context,
                                                                            const Columns& columns) {
    ColumnViewer value_viewer = ColumnViewer<LT>(columns[0]);
    ColumnViewer pk_hash_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer model_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row)) {
            result.append_null();
            continue;
        }
        auto model = AbcModel<LT>::create_model(model_viewer.value(row).to_string());
        if (!model.has_value()) {
            result.append_null();
            continue;
        }
        auto label = model->label(value_viewer.value(row), pk_hash_viewer.value(row));
        if (label == -1) {
            result.append_null();
        } else {
            result.append(label);
        }
    }
    return result.build(all_const);
}

template <LogicalType LT>
StatusOr<ColumnPtr> CelonisAbcModel<LT>::apply_abc_model_constant_model([[maybe_unused]] FunctionContext* context,
                                                                        const Columns& columns) {
    ColumnViewer value_viewer = ColumnViewer<LT>(columns[0]);
    ColumnViewer pk_hash_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    const auto* state = reinterpret_cast<const AbcModelStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));

    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || !state->model.has_value()) {
            result.append_null();
            continue;
        }
        auto label = state->model->label(value_viewer.value(row), pk_hash_viewer.value(row));
        if (label == -1) {
            result.append_null();
        } else {
            result.append(label);
        }
    }
    return result.build(all_const);
}

template <LogicalType LT>
StatusOr<ColumnPtr> CelonisAbcModel<LT>::apply_abc_model(FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(3, columns.size());
    const auto* state = reinterpret_cast<const AbcModelStateFragmentLocal<LT>*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

template class CelonisAbcModel<TYPE_BIGINT>;

template class CelonisAbcModel<TYPE_DOUBLE>;

} // namespace starrocks
