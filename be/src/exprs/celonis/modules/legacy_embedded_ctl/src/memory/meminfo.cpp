#include "legacy_embedded_ctl/memory/meminfo.h"

#include <fstream>
#include <limits>
#include <sstream>

#include <boost/interprocess/mapped_region.hpp>

#include "legacy_embedded_ctl/bits/memory_utils.h"
#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"
#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::legacy_embedded_ctl {

namespace {

/**
 * @brief Produces and returns a JSON object (@see legacy_embedded_format/json/json.h) from the given meminfo instance
 */
legacy_embedded_format::json::json_t to_json(const full_meminfo& mi) {
  static constexpr auto UNIT = byte_unit::KiB;
  legacy_embedded_format::json::json_t json{};
  json["total"] = mi.total<UNIT>();
  json["global"] = mi.in_use_global<UNIT>();
  json["process"] = mi.in_use_by_process<UNIT>();
  json["estimate"] = mi.current_estimate<UNIT>();
  return json;
}

struct total_and_available_memory {
  using size_type = meminfo::size_type;
  size_type total_in_bytes{0};
  size_type available_in_bytes{0};
};

[[nodiscard]] total_and_available_memory fetch_total_and_available_memory() {
  // get total and available physical memory space
  static constexpr const char* LINUX_MEMINFO_PATH{"/proc/meminfo"};
  using size_type = meminfo::size_type;
  size_type total_kib{0};
  size_type available_kib{0};
  std::string ignore{};  // placeholder variable when reading uninteresting tokens in LINUX_MEMINFO
  if (std::ifstream ifs{LINUX_MEMINFO_PATH}; ifs.good()) {
    ifs >> ignore >> total_kib >> ignore;  // total memory
    ifs >> ignore >> ignore >> ignore;     // free memory
    ifs >> ignore >> available_kib;        // available memory
    ifs.close();
  }
  return total_and_available_memory{convert_byte_size<byte_unit::KiB, byte_unit::B>(total_kib),
                                    convert_byte_size<byte_unit::KiB, byte_unit::B>(available_kib)};
}

[[nodiscard]] meminfo::size_type fetch_memory_in_use_by_process(const meminfo::size_type page_size_in_bytes) {
  // get resident memory space of current process
  static constexpr const char* LINUX_SELF_STATM_PATH{"/proc/self/statm"};
  using size_type = meminfo::size_type;
  size_type used_by_process_kib{0};
  std::string ignore{};  // placeholder variable when reading uninteresting tokens in LINUX_SELF_STATM
  if (std::ifstream ifs{LINUX_SELF_STATM_PATH}; ifs.good()) {
    size_t rss = 0;  // resident set size; number of pages the process has in real memory
    ifs >> ignore >> rss;
    ifs.close();
    used_by_process_kib = rss * convert_byte_size<byte_unit::B, byte_unit::KiB>(page_size_in_bytes);
  }
  return convert_byte_size<byte_unit::KiB, byte_unit::B>(used_by_process_kib);
}

[[nodiscard]] meminfo::size_type fetch_peak_memory_consumption_by_process() {
  try {
    // get peak memory consumption of current process
    static constexpr const char* LINUX_SELF_STATUS_PATH{"/proc/self/status"};
    size_t peak_consumption_of_process_kib{0};
    std::ifstream ifs{LINUX_SELF_STATUS_PATH};
    std::string token;
    while (ifs.good()) {
      ifs >> token;
      if (token == "VmHWM:") {
        ifs >> peak_consumption_of_process_kib;
        break;
      }
    }
    return convert_byte_size<byte_unit::KiB, byte_unit::B>(peak_consumption_of_process_kib);
  } catch (...) {
    // ignore
    return 0;
  }
}

}  // namespace

legacy_embedded_format::json::json_object_t meminfo_change_json(const full_meminfo& old_meminfo,
                                                                const full_meminfo& new_meminfo,
                                                                const std::optional<size_t> not_swapped_bytes,
                                                                const std::string& status) {
  legacy_embedded_format::json::json_object_t json{};
  auto& json_title{json["memory_info"]};
  json_title["status"] = status;
  if (not_swapped_bytes.has_value()) {
    json["not_swapped"] = not_swapped_bytes.value();
  }
  if (const auto old_usage_opt{old_meminfo.in_use_global_percentage()}; old_usage_opt.has_value()) {
    json_title["original_usage"] = *old_usage_opt;
  }
  if (const auto new_usage_opt{new_meminfo.in_use_global_percentage()}; new_usage_opt.has_value()) {
    json_title["new_usage"] = *new_usage_opt;
  }
  {
    const auto process_diff{static_cast<int64_t>(old_meminfo.in_use_by_process()) -
                            static_cast<int64_t>(new_meminfo.in_use_by_process())};
    const auto in_use_diff{static_cast<int64_t>(old_meminfo.in_use_global()) -
                           static_cast<int64_t>(new_meminfo.in_use_global())};

    json_title["original_memory_status"] = to_json(old_meminfo);

    auto& status_difference{json_title["status_difference"]};
    status_difference["global_diff"] = in_use_diff;
    status_difference["process_diff"] = process_diff;
  }
  return json;
}

meminfo fetch_current_meminfo() {
  const auto page_size_in_bytes{boost::interprocess::mapped_region::get_page_size()};
  const auto [total_in_bytes, available_in_bytes]{fetch_total_and_available_memory()};
  const auto in_use_by_process_in_bytes{fetch_memory_in_use_by_process(page_size_in_bytes)};
  return meminfo{page_size_in_bytes, total_in_bytes, available_in_bytes, in_use_by_process_in_bytes};
}

full_meminfo fetch_current_full_meminfo() {
  const auto page_size_in_bytes{boost::interprocess::mapped_region::get_page_size()};
  const auto [total_in_bytes, available_in_bytes]{fetch_total_and_available_memory()};
  const auto in_use_by_process_in_bytes{fetch_memory_in_use_by_process(page_size_in_bytes)};
  const auto peak_consumption_by_process_in_bytes{fetch_peak_memory_consumption_by_process()};
  const auto current_estimate_in_bytes{
      global_memory_consumption_tracker::get_consumption_tracker().cur_net_allocated()};
  return full_meminfo{page_size_in_bytes,
                      total_in_bytes,
                      available_in_bytes,
                      in_use_by_process_in_bytes,
                      peak_consumption_by_process_in_bytes,
                      current_estimate_in_bytes};
}

void log_if_net_allocated_nonzero() {
  auto& memory_consumption_tracker{global_memory_consumption_tracker::get_consumption_tracker()};
  if (memory_consumption_tracker.cur_net_allocated() != 0) {
    auto meminfo_state{fetch_current_meminfo()};
    details::log_memory_tracking_state(meminfo_state.available(), -1, meminfo_state.in_use_by_process());
  }
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
