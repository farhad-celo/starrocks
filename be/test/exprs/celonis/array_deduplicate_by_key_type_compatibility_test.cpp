#include <glog/logging.h>
#include <gtest/gtest.h>

#include <tuple>
#include <vector>

#include "column/column_helper.h"
#include "column/const_column.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/array_deduplicate_by_key.h"
#include "util.h"

namespace starrocks {

/**
 * Tests which target all valid key- and value type combinations of the operator.
 */
template <typename>
class CelonisDeduplicateByKeyTypeCompatibilityTest : public testing::Test {};

// Helper alias to wrap LogicalType enum values into compile-time types
template <LogicalType LT>
using LTConst = std::integral_constant<LogicalType, LT>;

using types = testing::Types<
        // BIGINT pairs
        std::tuple<LTConst<TYPE_BIGINT>, LTConst<TYPE_BIGINT>>, std::tuple<LTConst<TYPE_BIGINT>, LTConst<TYPE_DOUBLE>>,
        std::tuple<LTConst<TYPE_BIGINT>, LTConst<TYPE_VARCHAR>>,
        std::tuple<LTConst<TYPE_BIGINT>, LTConst<TYPE_DATETIME>>,
        // DOUBLE pairs
        std::tuple<LTConst<TYPE_DOUBLE>, LTConst<TYPE_BIGINT>>, std::tuple<LTConst<TYPE_DOUBLE>, LTConst<TYPE_DOUBLE>>,
        std::tuple<LTConst<TYPE_DOUBLE>, LTConst<TYPE_VARCHAR>>,
        std::tuple<LTConst<TYPE_DOUBLE>, LTConst<TYPE_DATETIME>>,
        // VARCHAR pairs
        std::tuple<LTConst<TYPE_VARCHAR>, LTConst<TYPE_BIGINT>>,
        std::tuple<LTConst<TYPE_VARCHAR>, LTConst<TYPE_DOUBLE>>,
        std::tuple<LTConst<TYPE_VARCHAR>, LTConst<TYPE_VARCHAR>>,
        std::tuple<LTConst<TYPE_VARCHAR>, LTConst<TYPE_DATETIME>>,
        // DATETIME pairs
        std::tuple<LTConst<TYPE_DATETIME>, LTConst<TYPE_BIGINT>>,
        std::tuple<LTConst<TYPE_DATETIME>, LTConst<TYPE_DOUBLE>>,
        std::tuple<LTConst<TYPE_DATETIME>, LTConst<TYPE_VARCHAR>>,
        std::tuple<LTConst<TYPE_DATETIME>, LTConst<TYPE_DATETIME>>>;

TYPED_TEST_SUITE(CelonisDeduplicateByKeyTypeCompatibilityTest, types);

TYPED_TEST(CelonisDeduplicateByKeyTypeCompatibilityTest, TestAllTypeCombinations) {
    // GIVEN
    constexpr LogicalType KeyTypeLT = std::tuple_element_t<0, TypeParam>::value;
    constexpr LogicalType ValueTypeLT = std::tuple_element_t<1, TypeParam>::value;

    using KeyType = RunTimeCppType<KeyTypeLT>;
    using ValueType = RunTimeCppType<ValueTypeLT>;

    auto key_arrays = ColumnHelper::create_column(celonis::array_type(KeyTypeLT), false);
    key_arrays->append_datum(DatumArray{KeyType{}, KeyType{}});

    auto value_arrays = ColumnHelper::create_column(celonis::array_type(ValueTypeLT), false);
    value_arrays->append_datum(DatumArray{ValueType{}, ValueType{}});

    constexpr auto expected_rows = 1;
    const auto expected = DatumArray{ValueType{}};

    // WHEN
    const auto result = CelonisArrayDeduplicateByKey<KeyTypeLT, ValueTypeLT>::array_deduplicate_by_key(
                                nullptr, {key_arrays, value_arrays})
                                .value();

    // THEN
    ASSERT_EQ(result->size(), expected_rows);
    ASSERT_EQ(result->get(0).get_array(), expected);
}

} // namespace starrocks
