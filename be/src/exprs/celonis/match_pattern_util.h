#pragma once

#include <optional>
#include <string_view>

namespace starrocks {

int64_t pattern_index(const char* const text, const char* const pattern, const int64_t occurrence);

} // namespace starrocks
