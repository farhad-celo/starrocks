#include "util.h"
#include "google/protobuf/util/json_util.h"

namespace starrocks::celonis {

TypeDescriptor array_type(const LogicalType& element_type) {
    TypeDescriptor t;
    t.type = TYPE_ARRAY;
    t.children.resize(1);
    t.children[0].type = element_type;
    t.children[0].len = (element_type == TYPE_VARCHAR || element_type == TYPE_CHAR) ? 20 : -1;
    return t;
}

std::string get_is_workdays_str(int n_days, const std::unordered_set<int>& one_indexes) {
    std::string sep = "";
    std::string rv;
    for (int i = 0; i < n_days; ++i) {
        // is_workday: {index}
        std::string item = "\"is_workday\": " + std::string(one_indexes.count(i) ? "true" : "false");
        rv += sep;
        sep = ", ";
        rv += item;
    }
    return rv;
}

std::string to_base64_encoded_string(const ::celonis::accelerator::Calendar& calendar_proto) {
    std::string binary_string;
    calendar_proto.SerializeToString(&binary_string);

    int cipher_len = (size_t)(4.0 * ceil((double) binary_string.length() / 3.0)) + 1;
    char p[cipher_len];

    int len = base64_encode2((unsigned char*) binary_string.data(), binary_string.length(), (unsigned char*)p);
    std::string encoded_string(p, len);
    return encoded_string;
}

std::optional<std::string> to_calendar_json_string(const std::string& encoded_string) {
    int cipher_len = encoded_string.length();
    std::unique_ptr<char[]> p;
    p.reset(new char[cipher_len + 3]);

    int len = base64_decode2(encoded_string.data(), encoded_string.length(), p.get());
    std::string decoded_string(p.get(), len);
    ::celonis::accelerator::Calendar calendar_proto;
    bool success = calendar_proto.ParseFromString(decoded_string);
    if (!success) {
        return std::nullopt;
    }
    std::string calendar_json;
    google::protobuf::util::MessageToJsonString(calendar_proto, &calendar_json);
    return calendar_json;
}

} // namespace starrocks::celonis
