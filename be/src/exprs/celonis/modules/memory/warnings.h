#pragma once

#include <memory>
#include <set>
#include <string>

/**
 * @deprecated The way of issuing warnings has changed. Consider making use of the framework provided by
 * 'common/warnings_container.h'
 */
namespace celonis::accelerator::memory {

static constexpr const char* NULL_COMPARISON_WARNING =
    "Comparison with NULL always returns NULL. To check for NULL values, please use <value> IS NULL or "
    "ISNULL(<value>)=1";

/**
 * Warnings are stored in a set to automatically filter out duplicate warnings.
 * Duplicate warnings occur for example for the reference operator which adds cached warnings to the scope which have
 * been already added to the scope by the referred operator.
 */
using warnings_container_t = std::set<std::string>;
using warnings_t = std::shared_ptr<warnings_container_t>;

}  // namespace celonis::accelerator::memory
