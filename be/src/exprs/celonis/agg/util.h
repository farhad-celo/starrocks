#pragma once

#include <optional>

#include "column/column.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

constexpr size_t DEFAULT_CELONIS_PROTO_SIZE_LIMIT = 1LL << 30; // 1GB

// Serializes a protobuf message to a base64-encoded string.
// If the serialized message size exceeds size_limit, returns std::nullopt.
// If compress is true, the serialized data is compressed before encoding.
std::optional<std::string> to_base64_encoded_string(const google::protobuf::Message& message, size_t size_limit,
                                                    bool compress);

void serialize_to_column(const std::unique_ptr<Column>& src, ColumnPtr& dst);

} // namespace starrocks
