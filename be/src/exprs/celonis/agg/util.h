#pragma once

#include "modules/query/calendars.pb.h"

namespace starrocks {

std::string to_base64_encoded_string(const celonis::accelerator::Calendar& calendar_proto);
} // namespace starrocks

