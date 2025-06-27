#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/celonis/util.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/base64.h"
#include <cmath>

namespace starrocks {

std::optional<std::string>
to_base64_encoded_string(const google::protobuf::Message& message, size_t size_limit, bool compress) {
    if (message.ByteSizeLong() > size_limit) {
        return std::nullopt;
    }
    std::string binary_string;
    message.SerializeToString(&binary_string);
    if (compress && !binary_string.empty()) {
        binary_string = std::move(compress_string(binary_string, true));
    }
    int cipher_len = (size_t) (4.0 * ceil((double) binary_string.length() / 3.0)) + 1;
    std::string p(cipher_len, '\0');

    int len = base64_encode3((unsigned char*) binary_string.data(), binary_string.length(), (unsigned char*) p.data());
    std::string encoded_string(p.data(), len);
    return encoded_string;
}

void serialize_to_column(const std::unique_ptr<Column>& src, ColumnPtr& dst) {
    auto elem_size = src->size();
    auto array_col = down_cast<ArrayColumn*>(ColumnHelper::get_data_column(dst.get()));
    if (dst->is_nullable()) {
        down_cast<NullableColumn*>(dst.get())->null_column_data().emplace_back(0);
    }
    if (src->only_null()) {
        array_col->elements_column()->append_nulls(elem_size);
    } else {
        auto& elements = array_col->elements_column();
        for (auto i = 0; i < elem_size; ++i) {
            elements->append_datum(src->get(i));
        }
    }
    auto& offsets = array_col->offsets_column()->get_data();
    offsets.push_back(offsets.back() + elem_size);
}

} // namespace starrocks

