#include "util.h"
#include "exprs/celonis/util.h"
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/stubs/strutil.h"

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

std::string get_workday_mask_str(int n_days, const std::unordered_set<int>& one_indexes) {
    size_t num_bytes = (n_days + 7) / 8;
    std::string mask_data(num_bytes, '\0');
    for (auto i = 0; i < n_days; ++i) {
        if (one_indexes.count(i)) {
            int byte_index = i / 8;
            int bit_index = i % 8;
            unsigned char bit_value_to_set = (1 << bit_index);
            mask_data[byte_index] = static_cast<char>(
                    static_cast<unsigned char>(mask_data[byte_index]) | bit_value_to_set
            );
        }
    }
    std::string base64_mask;
    google::protobuf::Base64Escape(mask_data, &base64_mask);
    std::string rv = "\"workday_mask\": \"" + base64_mask + "\"";
    return rv;
}

std::string to_base64_encoded_string(const ::celonis::accelerator::Calendar& calendar_proto, bool compress) {
    std::string binary_string;
    calendar_proto.SerializeToString(&binary_string);

    if (compress) {
        binary_string = std::move(compress_string(binary_string, true));
    }

    int cipher_len = (size_t)(4.0 * ceil((double) binary_string.length() / 3.0)) + 1;
    char p[cipher_len];

    int len = base64_encode3((unsigned char*) binary_string.data(), binary_string.length(), (unsigned char*)p);
    std::string encoded_string(p, len);
    return encoded_string;
}

std::optional<std::string> to_calendar_json_string(const std::string& encoded_string) {
    std::vector<char> decoded_buffer(encoded_string.length());
    int decoded_len = base64_decode3(encoded_string.data(), encoded_string.length(), decoded_buffer.data());
    // Check if the decoding was successful before attempting to parse.
    if (decoded_len < 0) {
        return std::nullopt;
    }
    std::string_view payload(decoded_buffer.data(), decoded_len);
    const char format_flag = !payload.empty() ? payload[0] : '\0';
    ::celonis::accelerator::Calendar calendar_proto;
    bool success;
    if (format_flag == ZLIB_COMPRESSED_FLAG) {
        payload.remove_prefix(1);
        std::string decompressed_data;
        if (!decompress_string(payload, decompressed_data)) {
            return std::nullopt;
        }
        success = calendar_proto.ParseFromString(decompressed_data);
    } else if (format_flag == UNCOMPRESSED_FLAG) {
        payload.remove_prefix(1);
        success = calendar_proto.ParseFromArray(payload.data(), payload.size());
    } else {
        success = calendar_proto.ParseFromArray(payload.data(), payload.size());
    }
    if (!success) {
        return std::nullopt;
    }
    std::string calendar_json;
    google::protobuf::util::MessageToJsonString(calendar_proto, &calendar_json);
    return calendar_json;
}

} // namespace starrocks::celonis
