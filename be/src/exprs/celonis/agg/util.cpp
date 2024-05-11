#include "exprs/celonis/agg/util.h"
#include "exprs/base64.h"
#include <cmath>

namespace starrocks {

std::string to_base64_encoded_string(const celonis::accelerator::Calendar &calendar_proto) {
    std::string binary_string;
    calendar_proto.SerializeToString(&binary_string);
    int cipher_len = (size_t) (4.0 * ceil((double) binary_string.length() / 3.0)) + 1;
    std::string p(cipher_len, '\0');

    int len = base64_encode2((unsigned char *) binary_string.data(), binary_string.length(), (unsigned char *) p.data());
    std::string encoded_string(p.data(), len);
    return encoded_string;
}

} // namespace starrocks

