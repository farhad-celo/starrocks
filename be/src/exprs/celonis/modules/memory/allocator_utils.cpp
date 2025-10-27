#include "allocator_utils.h"

#include <array>

#include <jemalloc/jemalloc.h>

#include "log/log.h"
#include "modules/common/exceptions.h"

#ifdef CELOSTAR
// TODO(j.kim): Sync jemalloc version or set a separate thirdparty libraries.
#define mallctl jemallctl
#define mallctlbymib jemallctlbymib
#define mallctlnametomib jemallctlnametomib
#endif

namespace celonis::accelerator::memory {

std::pair<std::array<size_t, 3>, size_t> init_purge_mib() {
  std::array<size_t, 3> mib_array{};
  size_t array_size = mib_array.size();
  mallctlnametomib("arena.0.purge", mib_array.data(), &array_size);
  mib_array.at(1) = MALLCTL_ARENAS_ALL;
  return std::make_pair(mib_array, array_size);
}

void release_unused_buffers() {
  static auto mib_pair = init_purge_mib();
  int error_code = mallctlbymib(mib_pair.first.data(), mib_pair.second, nullptr, nullptr, nullptr, 0);
  if (error_code != 0) {
    log::error("Releasing unused memory failed with error code: {}", error_code);
  };
}

std::optional<allocator_stats> get_allocator_stats() {
  allocator_stats stats{};
  auto throw_on_non_zero_return = [](int return_value) {
    if (return_value != 0) {
      throw common::internal_exception("Failure in get_allocator_stats");
    }
  };
  bool config_stats_enabled{false};
  size_t config_stats_size{sizeof(config_stats_enabled)};

  throw_on_non_zero_return(mallctl("config.stats", &config_stats_enabled, &config_stats_size, nullptr, 0));
  if (!config_stats_enabled) {
    return std::nullopt;
  }
  uint64_t epoch{0};
  size_t epoch_size{sizeof(epoch)};
  throw_on_non_zero_return(mallctl("epoch", &epoch, &epoch_size, &epoch, epoch_size));

  size_t size{sizeof(size_t)};
  throw_on_non_zero_return(mallctl("stats.allocated", &stats.allocated, &size, nullptr, 0));
  throw_on_non_zero_return(mallctl("stats.active", &stats.active, &size, nullptr, 0));
  throw_on_non_zero_return(mallctl("stats.metadata", &stats.metadata_size, &size, nullptr, 0));
  throw_on_non_zero_return(mallctl("stats.resident", &stats.resident, &size, nullptr, 0));
  throw_on_non_zero_return(mallctl("stats.mapped", &stats.mapped, &size, nullptr, 0));
  throw_on_non_zero_return(mallctl("stats.retained", &stats.retained, &size, nullptr, 0));

  return stats;
}

}  // namespace celonis::accelerator::memory
