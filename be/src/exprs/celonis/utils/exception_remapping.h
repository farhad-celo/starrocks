#pragma once

#include <cpml/exception.h>
#include <ctl/exception.h>
#include <fmt/format.h>

#include "common/status.h"

namespace starrocks::celonis {

[[nodiscard]] Status execute_and_return_status(auto callable, const std::string_view operator_name) {
    try {
        callable();
        return Status::OK();
    } catch (const cpml::invalid_argument& ex) {
        return Status::InvalidArgument(fmt::format("CPML invalid argument error during {} execution: '{}'",
                                                   operator_name, ex.internal_message()));
    } catch (const cpml::cpml_exception& ex) {
        return Status::RuntimeError(
                fmt::format("CPML error during {} execution: '{}'", operator_name, ex.internal_message()));
    } catch ([[maybe_unused]] const ctl::bad_alloc& ex) {
        throw std::bad_alloc{}; // rethrow as std::bad_alloc s.t. SR memory tracker will handle it
    } catch (const ctl::base_exception& ex) {
        return Status::RuntimeError(
                fmt::format("Error during {} execution: '{}'", operator_name, ex.internal_message()));
    }
    // all other exceptions are propagated
}

} // namespace starrocks::celonis
