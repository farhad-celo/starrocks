#pragma once

#include <cstddef>

#include "legacy_embedded_ctl/checked_ptr.h"

namespace celonis::accelerator::legacy_embedded_ctl {

class memory_tracking_strategy;

using abstract_strategy_t = legacy_embedded_ctl::checked_shared_ptr<memory_tracking_strategy>;

/*
 * A class representing a memory tracking strategy. Memory tracking strategies are designed to be used in a stack, each
 * owning a polymorphic pointer to an upstream strategy. Their design is analogous to polymorphic memory resources, only
 * that they do not perform an allocation at the end of the stream.
 */
class memory_tracking_strategy {
 public:
  explicit memory_tracking_strategy(abstract_strategy_t downstream_tracking_strategy = nullptr) noexcept;
  virtual ~memory_tracking_strategy() = default;

  virtual void register_allocation(std::size_t bytes) = 0;

  virtual void deregister_allocation(std::size_t bytes) = 0;

  [[nodiscard]] virtual bool is_equal(const memory_tracking_strategy& other) const = 0;

  [[nodiscard]] static abstract_strategy_t get_default_strategy() noexcept;

  [[nodiscard]] abstract_strategy_t get_downstream_strategy() const;

 protected:
  void register_downstream(std::size_t bytes) const;

  void deregister_downstream(std::size_t bytes) const;

  [[nodiscard]] bool downstream_equal(const memory_tracking_strategy& other) const;

 private:
  abstract_strategy_t downstream_strategy_;
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
