#pragma once

#include <optional>
#include "column/column.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

std::optional<std::string> to_base64_encoded_string(const google::protobuf::Message& message);

void serialize_to_column(const std::unique_ptr<Column>& src, ColumnPtr& dst);

} // namespace starrocks

