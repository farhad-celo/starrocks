#include "swap_info.h"

#include <ctl/assert.h>

namespace celonis::accelerator::memory::management {

static constexpr const char* TMP_SWAP_FILE_SUFFIX{".tmp"};

std::string get_tmp_swap_file_name(const std::string& swap_file) { return swap_file + TMP_SWAP_FILE_SUFFIX; }

std::string swap_info::get_swap_file_path(const std::string& swap_file) const {
  return base_directory() + "/" + swap_file;
}

std::string swap_info::get_tmp_swap_file_path(const std::string& swap_file) const {
  return base_directory() + "/" + get_tmp_swap_file_name(swap_file);
}

swap_info swap_info::swap_into_sub_dir(const std::string& sub_dir, const persistence_status persistence_state) const {
  auto sinfo = copy_and_set_persistence(persistence_state);
  sinfo.swap_base_directory.append("/" + sub_dir);
  return sinfo;
}

swap_info swap_info::copy_and_set_persistence(const persistence_status persistence_state) const {
  auto cpy{*this};
  if (cpy.persistence_state() != persistence_state) {
    cpy.swap_persistence_state = persistence_state;
  }
  return cpy;
}

swap_info swap_info::swap_into_sub_dir(const std::string& sub_dir) const {
  return swap_into_sub_dir(sub_dir, persistence_state());
}

swap_info no_swap() { return swap_info{}; }

}  // namespace celonis::accelerator::memory::management
