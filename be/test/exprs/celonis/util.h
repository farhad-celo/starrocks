#pragma once

#include "column/column.h"
#include "column/datum.h"
#include "column/type_traits.h"
#include "column/vectorized_fwd.h"
#include "runtime/types.h"
#include "types/logical_type.h"

#include <gtest/gtest.h>

namespace starrocks::celonis {
// Creates an array type from the given 'element_type'
TypeDescriptor array_type(const LogicalType& element_type);

template <LogicalType LT>
class TestEvaluator {
public:
    TestEvaluator() {}

    void add_expected(Datum expected) {
        expected_.push_back(std::move(expected));
    }

    void evaluate(ColumnPtr result) {
        ASSERT_EQ(result->size(), expected_.size());
        for (int row = 0; row < result->size(); row++) {
            if (result->get(row).is_null() || expected_[row].is_null()) {
                EXPECT_EQ(result->get(row).is_null(), expected_[row].is_null()) << "row: " << row;
                continue;
            }
            auto result_array = result->get(row).get_array();
            auto expected_array = expected_[row].get_array();
            ASSERT_EQ(result_array.size(), expected_array.size()) << "row: " << row;
            for (int i = 0; i < result_array.size(); i++) {
                if (result_array[i].is_null()) {
                    EXPECT_TRUE(expected_array[i].is_null()) << "row: " << row << ", index: " << i
                                                             << ", result: null, expected: "
                                                             << expected_array[i].get<RunTimeCppType<LT>>();
                } else if (expected_array[i].is_null()) {
                    EXPECT_TRUE(result_array[i].is_null()) << "row: " << row << ", index: " << i << ", result: "
                                                           << result_array[i].get<RunTimeCppType<LT>>()
                                                           << ", expected: null";
                } else {
                    EXPECT_EQ(result_array[i].get<RunTimeCppType<LT>>(),
                              expected_array[i].get<RunTimeCppType<LT>>()) << "row: " << row << ", index: " << i;
                }
            }
        }
    }

private:
    std::vector<Datum> expected_;
};

} // namespace starrocks::celonis
