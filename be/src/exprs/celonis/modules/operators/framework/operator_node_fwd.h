#pragma once

namespace celonis::accelerator::operators::framework {

class common_table_result;
class operator_node;

using operator_node_t = std::unique_ptr<operator_node>;
using operator_node_pointers_t = std::vector<operators::framework::operator_node*>;

}  // namespace celonis::accelerator::operators::framework
