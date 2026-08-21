#pragma once

#include <chrono>

#include <ctl/named_type.h>

namespace celonis::accelerator::memory {

/**
 * @brief The clock type and time type used in the context of the memory module (e.g., for data handlers to report
 * access times, load times, ...)
 */
using mem_clock_t = std::chrono::steady_clock;
using mem_time_t = mem_clock_t::time_point;

using usage_time_t = ctl::named_type<mem_time_t, struct usage_time_tag, ctl::comparable,
                                     ctl::implicitly_convertible_to<mem_time_t>::templ>;
using load_time_t = ctl::named_type<mem_time_t, struct load_time_tag, ctl::comparable,
                                    ctl::implicitly_convertible_to<mem_time_t>::templ>;

using zero_init_t = ctl::named_type<bool, struct zero_init_tag>;

}  // namespace celonis::accelerator::memory
