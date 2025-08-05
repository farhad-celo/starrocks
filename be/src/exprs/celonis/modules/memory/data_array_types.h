#pragma once

#include <memory>
#include <variant>

#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/shared_types.h"
#include "modules/memory/management/const_data_accessor.h"

namespace celonis::accelerator::memory {

using data_array_types_t =
    std::variant<legacy_embedded_ctl::static_array<cel_string_t>, legacy_embedded_ctl::static_array<cel_int_t>, legacy_embedded_ctl::static_array<cel_float_t>,
                 legacy_embedded_ctl::static_array<cel_date_t>, legacy_embedded_ctl::static_array<cel_boolean_t>, legacy_embedded_ctl::static_array<cel_null_t>,
                 legacy_embedded_ctl::static_array<cel_uuid_t>>;

using const_data_array_types_t = std::variant<
    memory::management::const_data_accessor<cel_string_t>, memory::management::const_data_accessor<cel_int_t>,
    memory::management::const_data_accessor<cel_float_t>, memory::management::const_data_accessor<cel_date_t>,
    memory::management::const_data_accessor<cel_boolean_t>, memory::management::const_data_accessor<cel_uuid_t>,
    memory::management::const_data_accessor<cel_null_t>>;

}  // namespace celonis::accelerator::memory
