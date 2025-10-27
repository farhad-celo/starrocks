#include "nullable_pql_value.h"

#include "modules/memory/column.h"
#include "modules/memory/column_iterable.h"

namespace celonis::accelerator::utils {

namespace {

template <typename T>
[[nodiscard]] nullable_vec_t<T> to_nullable_vec_impl(const memory::column_t& column) {
    const common::execution_context context{"to_nullable_vec_impl"};
    return std::visit(
            [](const auto& iterable) {
                utils::nullable_vec_t<T> result{};
                result.reserve(std::distance(iterable.begin(), iterable.end()));
                std::copy(iterable.begin(), iterable.end(), std::back_inserter(result));
                return result;
            },
            memory::to_iterable<T>(column, context));
}

} // namespace

std::ostream& operator<<(std::ostream& os, const nullable_vec_variant& vec) {
    std::visit([&os](const auto& vec) { os << vec; }, vec);
    return os;
}

std::ostream& operator<<(std::ostream& os, const memory::column_t& column) {
    switch (column->get_data_type()) {
    case cel_int:
        return os << to_nullable_vec_impl<cel_int_t>(column);
    case cel_string:
        return os << to_nullable_vec_impl<cel_string_t>(column);
    case cel_float:
        return os << to_nullable_vec_impl<cel_float_t>(column);
    case cel_date:
        return os << to_nullable_vec_impl<cel_date_t>(column);
    case cel_boolean:
        return os << to_nullable_vec_impl<cel_boolean_t>(column);
    case cel_uuid:
        return os << to_nullable_vec_impl<cel_uuid_t>(column);
    case cel_null:
        return os << fmt::format("{{NULL column with {} values}}", column->get_row_count());
    }

    legacy_embedded_ctl::assert_unreachable();
}

nullable_vec_variant to_nullable_vec(const memory::column_t& column) {
    switch (column->get_data_type()) {
    case cel_int:
        return to_nullable_vec_impl<cel_int_t>(column);
    case cel_string:
        return to_nullable_vec_impl<cel_string_t>(column);
    case cel_float:
        return to_nullable_vec_impl<cel_float_t>(column);
    case cel_date:
        return to_nullable_vec_impl<cel_date_t>(column);
    case cel_boolean:
        return to_nullable_vec_impl<cel_boolean_t>(column);
    case cel_uuid:
        return to_nullable_vec_impl<cel_uuid_t>(column);
    case cel_null:
        throw common::internal_exception{"NULL column type is not supported."};
    }

    legacy_embedded_ctl::assert_unreachable();
}

} // namespace celonis::accelerator::utils

namespace celonis::accelerator::memory {
std::ostream& operator<<(std::ostream& os, const celonis::accelerator::memory::column_t& column) {
    return celonis::accelerator::utils::operator<<(os, column);
}

} // namespace celonis::accelerator::memory
