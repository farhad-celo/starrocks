#pragma once

#include <chrono>
#include <future>
#include <string_view>

#include "modules/memory/cache/cache_entry.h"
#include "modules/memory/column.h"

namespace celonis::accelerator::memory::cache {

static constexpr std::string_view CORRUPTION_REASON_EMPTY{};
static constexpr std::string_view CORRUPTION_REASON_INVALID_FUTURE{"invalid future"};
static constexpr std::string_view CORRUPTION_REASON_BAD_ALLOCATION{"bad allocation"};
static constexpr std::string_view CORRUPTION_REASON_BROKEN_SWAP_FILE{"broken swap file"};
static constexpr std::string_view CORRUPTION_REASON_TRANSIENT_EXCEPTION{"transient exception"};

struct is_corrupt_result {
  bool operator==(const is_corrupt_result& other) const {
    return is_corrupt == other.is_corrupt && corrupt_reason == other.corrupt_reason;
  }
  bool is_corrupt;
  std::string_view corrupt_reason;
};

template <typename CACHE_ENTRY_TYPE>
is_corrupt_result is_corrupt_entry(const std::shared_future<CACHE_ENTRY_TYPE>& cache_entry) {
  if (!cache_entry.valid()) {
    return {true, CORRUPTION_REASON_INVALID_FUTURE};
  }

  if (cache_entry.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      cache_entry.get();
    } catch (const legacy_embedded_ctl::oom_error& e) {
      return {true, CORRUPTION_REASON_BAD_ALLOCATION};
    } catch (const std::bad_alloc& e) {
      return {true, CORRUPTION_REASON_BAD_ALLOCATION};
    } catch (const legacy_embedded_ctl::retryable_error& ex) {
      return {true, CORRUPTION_REASON_TRANSIENT_EXCEPTION};
    } catch (const legacy_embedded_ctl::base_exception& e) {
      return {false, CORRUPTION_REASON_EMPTY};
    } catch (...) {
      return {false, CORRUPTION_REASON_EMPTY};
    }

    if constexpr (std::is_same_v<CACHE_ENTRY_TYPE, cache::cache_entry>) {
      if (cache_entry.get().column->swap_file_broken()) {
        return {true, CORRUPTION_REASON_BROKEN_SWAP_FILE};
      }
    }
  }

  return {false, CORRUPTION_REASON_EMPTY};
}

}  // namespace celonis::accelerator::memory::cache