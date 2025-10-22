#include <gtest/gtest.h>
#include <tbb/parallel_for.h>
#include <stdexcept>

namespace starrocks {

// Tests that an exception thrown within a tbb::parallel_for loop is
// correctly propagated and can be caught by the calling thread.
TEST(TbbTest, TestExceptionPropagation) {
    // TBB is expected to catch an exception from a worker thread
    // and rethrow it on the main thread. ASSERT_THROW verifies this behavior.
    ASSERT_THROW(
        tbb::parallel_for(0, 10, [](int i) {
            if (i == 8) { // Throw from an arbitrary iteration.
                throw std::runtime_error("TBB test exception");
            }
        }),
        std::runtime_error
    );
}

}
