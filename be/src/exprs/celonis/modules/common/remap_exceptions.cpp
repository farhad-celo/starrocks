#include "remap_exceptions.h"

namespace celonis::accelerator::common {
cpm_exception remap_to_cpm_exception(const cpml::invalid_argument& original_exception) {
  cpm_exception remapped{original_exception.external_message()};
  remapped.add_or_overwrite_cause(original_exception);
  return remapped;
}
}  // namespace celonis::accelerator::common
