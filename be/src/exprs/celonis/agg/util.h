#pragma once

#include <optional>
#include "modules/query/calendars.pb.h"

namespace starrocks {

std::optional<std::string> to_base64_encoded_string(const google::protobuf::Message& message);
} // namespace starrocks

