#include "sr_context.h"

#include <cpml/context/logging_context.h>
#include <cpml/context/memory_context.h>
#include <cpml/context/tracing_context.h>

namespace starrocks::celonis::cpml_utils {

namespace {

/**
 * @brief Implementation of the CPML's generic logging interface
 * TODO(n.weber): Implement if logging from within the CPML is needed
 */
class sr_logging_context final : public cpml::context::logging_context {
public:
    void jdebug([[maybe_unused]] std::string_view message,
                [[maybe_unused]] const format::json::json_object_t& details) const override {}
    void jinfo([[maybe_unused]] std::string_view message,
               [[maybe_unused]] const format::json::json_object_t& details) const override {}
    void jwarn([[maybe_unused]] std::string_view message,
               [[maybe_unused]] const format::json::json_object_t& details) const override {}
    void jerror([[maybe_unused]] std::string_view message,
                [[maybe_unused]] const format::json::json_object_t& details) const override {}
};

/** Allocations are made via global operator new */
using sr_memory_context = cpml::context::default_memory_context;
/** No-op tracing. TODO(n.weber): Implement if tracing from within the CPML is needed */
using sr_tracing_context = cpml::context::default_tracing_context;

} // anonymous namespace

cpml::context::function_context make_sr_function_context() {
    static const auto sr_logging_ctx{std::make_shared<sr_logging_context>()};
    static const auto sr_memory_ctx{std::make_shared<sr_memory_context>()};
    auto sr_tracing_ctx{std::make_unique<sr_tracing_context>()};
    return cpml::context::function_context{sr_logging_ctx, sr_memory_ctx, std::move(sr_tracing_ctx)};
}

} // namespace starrocks::celonis::cpml_utils
