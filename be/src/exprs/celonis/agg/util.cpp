#include "exprs/celonis/agg/util.h"
#include "exprs/base64.h"
#include <cmath>

namespace starrocks {

std::string to_base64_encoded_string(const celonis::accelerator::Calendar &calendar_proto) {
    std::string binary_string;
    calendar_proto.SerializeToString(&binary_string);
    int cipher_len = (size_t) (4.0 * ceil((double) binary_string.length() / 3.0)) + 1;
    char p[cipher_len];

    int len = base64_encode2((unsigned char *) binary_string.data(), binary_string.length(), (unsigned char *) p);
    std::string encoded_string(p, len);
    return encoded_string;
}

} // namespace starrocks

