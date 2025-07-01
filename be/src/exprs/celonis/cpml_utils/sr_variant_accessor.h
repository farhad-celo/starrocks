#pragma once

#include <cpml/variant/variant_accessor.h>

#include "exprs/celonis/agg/variant.h"

namespace starrocks::celonis::cpml_utils {

/** Implementation of the CPML's generic variant accessor interface for the Starrocks variant representation */
class sr_variant_accessor final : public cpml::variant::variant_accessor {
public:
    explicit sr_variant_accessor(const Variants& sr_variants);
    [[nodiscard]] size_type size() const override;
    [[nodiscard]] value_type operator[](cpml::variant::variant_id_t id) const override;
    [[nodiscard]] std::size_t count(cpml::variant::variant_id_t id) const override;

private:
    const Variants& sr_variants_;
};

} // namespace starrocks::celonis::cpml_utils