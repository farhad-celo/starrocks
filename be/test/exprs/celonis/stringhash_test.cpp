#include "exprs/celonis/stringhash.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"

namespace starrocks {

class CelonisStringhashTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(CelonisStringhashTest, empty_input) {
    auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), false);
    const auto result = CelonisStringhash::stringhash(nullptr, {strings}).value();
    EXPECT_EQ(0, result->size());
}

TEST_F(CelonisStringhashTest, normal_cases) {
    auto strings = ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true);
    strings->append_datum("LaTeX");
    strings->append_datum("PQL");
    strings->append_datum("Python");
    strings->append_datum("a ");
    strings->append_datum(" ");
    strings->append_datum("");
    strings->append_datum(kNullDatum);
    strings->append_datum("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-=_+[]\\{}|;");
    strings->append_datum("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz012345");
    const auto result = CelonisStringhash::stringhash(nullptr, {strings}).value();
    ASSERT_EQ(strings->size(), result->size());
    EXPECT_EQ("cScbqCayViK/QywnqgPUP9MiW6rjPelytv7oRtJ", result->get(0).get_slice());
    EXPECT_EQ("vD9Rq2vcAOaQdT0A2/OV9yyjjYX+HrdfrlUVU02", result->get(1).get_slice());
    EXPECT_EQ("o5cGXGnMp4ug1936A/VL/R2VA19PEgOVYcw80v2", result->get(2).get_slice());
    EXPECT_EQ("zYfBymiqp8llFFfzVKe85y2JUo6XDJqQ4/IfScg", result->get(3).get_slice());
    EXPECT_EQ("6CRFGgx7qL8rK9qOomRISAfWysVHcpaC/k1WC1C", result->get(4).get_slice());
    EXPECT_EQ("aSF6MHmQgJThESHQQjVKfB9VtkgsoaUeGyUN/R7", result->get(5).get_slice());
    EXPECT_TRUE(result->get(6).is_null());
    EXPECT_EQ("yJ0l74olqCL02HTbzkPUucdFmtD9Amf0Uw0zNXn", result->get(7).get_slice());
    EXPECT_EQ("SBYuE+grqRe7Th5KibfLEPYoC3rBOt9gjFUPH8A", result->get(8).get_slice());
}

} // namespace starrocks
