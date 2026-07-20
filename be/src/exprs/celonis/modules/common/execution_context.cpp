#include "execution_context.h"

namespace celonis::accelerator::common {

execution_context::execution_context() : span_{std::make_shared<tracing::span>()} {}

execution_context::execution_context(const std::string& operation_name)
    : span_{std::make_shared<tracing::span>(operation_name, tracing::tags_t{})} {}

execution_context::execution_context(tracing::span&& managed_span, const execution_context* parent)
    : parent_{parent}, span_{std::make_shared<tracing::span>(std::move(managed_span))} {}

execution_context::execution_context(const execution_context* parent) : parent_{parent}, span_{parent_->span_} {}

execution_context execution_context::create_sub_context(const std::string& operation_name,
                                                        const tracing::tags_t& tags) const {
  return execution_context{span_->start_child_span(operation_name, tags), this};
}

execution_context::execution_context(execution_context&& other_context) noexcept
    : span_{std::move(other_context.span_)} {}

execution_context::~execution_context() = default;

}  // namespace celonis::accelerator::common
