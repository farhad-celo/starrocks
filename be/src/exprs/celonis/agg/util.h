#pragma once

#include <optional>
#include "column/column.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

// Converts proto message to base64 encoded string, if the message size is greater than size_limit, return std::nullopt.
// Default size_limit is 1GB.
std::optional<std::string>
to_base64_encoded_string(const google::protobuf::Message& message, size_t size_limit = (1LL << 30));

void serialize_to_column(const std::unique_ptr<Column>& src, ColumnPtr& dst);

} // namespace starrocks

