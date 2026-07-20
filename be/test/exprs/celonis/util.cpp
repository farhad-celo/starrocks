#include "util.h"

namespace starrocks::celonis {

TypeDescriptor array_type(const LogicalType& element_type) {
    TypeDescriptor t;
    t.type = TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == TYPE_VARCHAR || element_type == TYPE_CHAR) ? 20 : -1;
    return t;
}

} // namespace starrocks::celonis
