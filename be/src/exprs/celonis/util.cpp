#include "exprs/celonis/util.h"

#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <sstream>
#include <string>

#include "column/array_column.h"
#include "column/column_helper.h"
#include "util/xxh3.h"

namespace starrocks {

uint128_t xx_hash3_128(const void* key, int32_t len, uint128_t seed) {
    const auto result = XXH3_128bits_withSeed(key, len, seed);
    return (uint128_t(result.high64) << 64) | result.low64;
}

bool is_ratio_invalid(double ratio) {
    return ratio < -EPS || ratio > 1.0 + EPS;
}

std::string compress_string(const std::string& data, bool add_flag) {
    namespace io = boost::iostreams;

    std::stringstream original_string_stream(data);
    std::stringstream compressed_string_stream;

    io::filtering_streambuf<io::input> out;
    out.push(io::zlib_compressor(io::zlib::best_compression));
    out.push(original_string_stream);

    io::copy(out, compressed_string_stream);
    std::string compressed_data = compressed_string_stream.str();

    if (add_flag) {
        return std::string(1, ZLIB_COMPRESSED_FLAG) + compressed_data;
    }
    return compressed_data;
}

bool decompress_string(std::string_view compressed_data, std::string& decompressed_output) {
    namespace io = boost::iostreams;
    if (!compressed_data.empty() && compressed_data[0] == ZLIB_COMPRESSED_FLAG) {
        compressed_data.remove_prefix(1);
    }
    try {
        std::stringstream compressed_stream;
        compressed_stream.write(compressed_data.data(), compressed_data.size());

        std::stringstream decompressed_stream;
        io::filtering_streambuf<io::input> in;
        in.push(io::zlib_decompressor());
        in.push(compressed_stream);

        io::copy(in, decompressed_stream);
        decompressed_output = decompressed_stream.str();
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

int128_t safe_abs(int128_t value) {
    if (value == std::numeric_limits<int128_t>::min()) {
        return std::numeric_limits<int128_t>::max();
    } else {
        return value < 0 ? -value : value;
    }
}

int64_t safe_abs(int64_t value) {
    if (value == std::numeric_limits<int64_t>::min()) {
        return std::numeric_limits<int64_t>::max();
    } else {
        return value < 0 ? -value : value;
    }
}

const ArrayColumn& extract_array_column(const Column* input_column) {
    return *(down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_column)));
}

UnnestedArrayData prepare_array_input(const Column* input_array) {
    UnnestedArrayData result;
    const NullableColumn* nullable_array = nullptr;
    if (input_array->is_nullable()) {
        nullable_array = down_cast<const NullableColumn*>(input_array);
        input_array = nullable_array->data_column().get();
        result.null_arrays = &(nullable_array->null_column()->get_data());
    }
    const auto& array_column = extract_array_column(input_array);
    result.offsets = &array_column.offsets();
    result.elements = &array_column.elements();

    // Indicates that the column has actual NULLs (a column can be Nullable and have no NULL elements).
    bool has_null = result.elements->has_null();
    if (has_null) {
        result.null_elements = &(down_cast<const NullableColumn*>(result.elements)->null_column()->get_data());
    }
    if (auto nullable = dynamic_cast<const NullableColumn*>(result.elements); nullable != nullptr) {
        result.elements = nullable->data_column().get();
    }
    return result;
}

std::string double_to_string(double value, int precision) {
    std::ostringstream oss;
    oss.precision(precision);
    oss << std::fixed << value;
    return oss.str();
}

} // namespace starrocks
