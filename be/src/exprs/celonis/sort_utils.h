#pragma once

#include "exec/sorting/sort_helper.h"

namespace starrocks {
namespace celonis {

/* Used to configure sort order of an order by column. */
enum class SortOrder { ASC, DESC };

/* Used to configure how null values are respected during ordering of a column. */
enum class NullHandling { NULLS_FIRST, NULLS_LAST };

/* Used to represent the result of a comparison. */
enum class CmpResult { LESS = -1, EQUAL = 0, GREATER = 1 };

struct SortDescriptor {
    SortOrder sort_order;
    NullHandling null_handling;
};

template <typename T>
concept ComparableType = std::is_arithmetic_v<T> || std::is_same_v<T, Slice> || std::is_same_v<T, DateValue> ||
        std::is_same_v<T, TimestampValue> || std::is_same_v<T, DecimalV2Value>;

struct DatumComparator {
    inline CmpResult operator()(Datum& lhs, Datum& rhs, SortDescriptor sort_desc);
};

/* Implementation section */

namespace {

template <typename T>
CmpResult cmp(const T& lhs, const T& rhs) requires ComparableType<T> {
    return static_cast<CmpResult>(SorterComparator<T>::compare(lhs, rhs));
}

template <typename T>
requires std::is_arithmetic_v<T> CmpResult cmp(const T& lhs, const T& rhs)
requires ComparableType<T> {
    return static_cast<CmpResult>(SorterComparator<T>::compare(lhs, rhs));
}

CmpResult cmp(starrocks::Datum& lhs, starrocks::Datum& rhs, NullHandling null_handling) {
    CmpResult ret;
    lhs.visit([&](const auto& variant) {
        ret = std::visit(overloaded{// Case 1: lhs is not null, but a comparable type
                                    [&](const ComparableType auto& lhs_val) {
                                        using ValueType = std::decay_t<decltype(lhs_val)>;
                                        if (rhs.is_null()) {
                                            return null_handling == NullHandling::NULLS_FIRST ? CmpResult::GREATER
                                                                                              : CmpResult::LESS;
                                        }
                                        const auto& rhs_val{rhs.get<ValueType>()};
                                        return cmp(lhs_val, rhs_val);
                                    },
                                    // Case 2: lhs is null
                                    [&](std::monostate /*lhs_val*/) {
                                        if (rhs.is_null()) {
                                            return CmpResult::EQUAL;
                                        }
                                        return null_handling == NullHandling::NULLS_FIRST ? CmpResult::LESS
                                                                                          : CmpResult::GREATER;
                                    },
                                    // Case 3: Unsupported type
                                    [](const auto& /*val_with_unsupported_type*/) -> CmpResult {
                                        throw std::runtime_error{"Encountered unsupported type in DatumComparator."};
                                    }},
                         variant);
    });
    return ret;
}
} // namespace

CmpResult DatumComparator::operator()(starrocks::Datum& lhs, starrocks::Datum& rhs,
                                      starrocks::celonis::SortDescriptor sort_desc) {
    if (sort_desc.sort_order == SortOrder::ASC) {
        return cmp(lhs, rhs, sort_desc.null_handling);
    }
    return cmp(rhs, lhs, sort_desc.null_handling);
}
} // namespace celonis
} // namespace starrocks