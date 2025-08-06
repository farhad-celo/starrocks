#pragma once

#include <vector>

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

class unfolding_event;
struct unfolding_data;

using local_configuration_t = std::vector<unfolding_event*>;
using foata_normal_form_t = std::vector<local_configuration_t>;

struct unfolding_data;
class unfolding_net;

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
