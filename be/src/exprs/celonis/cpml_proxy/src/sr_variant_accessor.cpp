#include "sr_variant_accessor.h"

#include <fmt/format.h>

#include <concepts>

namespace cpml_proxy {

namespace {

using cpml::variant::variant_id_t;

template <std::signed_integral SIGNED_T, typename UNSIGNED_T = std::make_unsigned_t<SIGNED_T>>
[[nodiscard]] UNSIGNED_T as_unsigned(const SIGNED_T value) {
    if (std::in_range<UNSIGNED_T>(value)) {
        return static_cast<UNSIGNED_T>(value);
    }
    throw std::runtime_error{fmt::format("Value [{}] can not be made unsigned.", value)};
}

} // anonymous namespace

sr_variant_accessor::sr_variant_accessor(const starrocks::Variants& sr_variants) : sr_variants_{sr_variants} {}

sr_variant_accessor::size_type sr_variant_accessor::size() const {
    return sr_variants_.size();
}

sr_variant_accessor::value_type sr_variant_accessor::operator[](variant_id_t id) const {
    return sr_variants_[as_unsigned(id.get())].variant;
}

std::size_t sr_variant_accessor::count(variant_id_t id) const {
    return sr_variants_[as_unsigned(id.get())].count;
}

} // namespace cpml_proxy
