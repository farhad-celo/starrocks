#pragma once

#include <type_traits>

/**
 * Implementation for ctl::is_base_of_template based on https://stackoverflow.com/a/34672753
 */
namespace celonis::accelerator::ctl::details {

template <template <typename...> class BASE_TEMPLATE, typename... DERIVED_TEMPLATE_IMPL>
std::true_type is_base_of_template_impl(const BASE_TEMPLATE<DERIVED_TEMPLATE_IMPL...>*);

template <template <typename...> class BASE_TEMPLATE>
std::false_type is_base_of_template_impl(...);

}  // namespace celonis::accelerator::ctl::details