#pragma once

#include <cpml/context/function_context.h>

namespace starrocks::celonis::cpml_utils {

[[nodiscard]] cpml::context::function_context make_sr_function_context();

} // namespace starrocks::celonis::cpml_utils
