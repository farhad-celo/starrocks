#include "exprs/celonis/util.h"

#include <limits>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "exprs/celonis/agg/util.h"
#include "exprs/celonis/base64.h"

namespace starrocks {

std::optional<std::string> to_base64_encoded_string(const google::protobuf::Message& message, size_t size_limit,
                                                    bool compress) {
    if (message.ByteSizeLong() > size_limit) {
        return std::nullopt;
    }
    std::string binary_string;
    if (!message.SerializeToString(&binary_string)) {
        return std::nullopt;
    }
    if (compress && !binary_string.empty()) {
        binary_string = std::move(compress_string(binary_string, true));
    }

    const size_t encoded_blocks = binary_string.size() / 3 + (binary_string.size() % 3 != 0);
    if (encoded_blocks > std::numeric_limits<size_t>::max() / 4) {
        return std::nullopt;
    }
    const size_t encoded_size = encoded_blocks * 4;
    std::string encoded_string(encoded_size, '\0');
    const size_t actual_encoded_size =
            base64_encode3(reinterpret_cast<const unsigned char*>(binary_string.data()), binary_string.size(),
                           reinterpret_cast<unsigned char*>(encoded_string.data()));
    if (actual_encoded_size != encoded_size) {
        return std::nullopt;
    }
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
