#pragma once

#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/source_location.h"
#include "legacy_embedded_ctl/type_traits.h"
#include "modules/memory/column_fwd.h"
#include "modules/operators/framework/operator_node_fwd.h"

namespace celonis::accelerator::operators {

namespace details {

/**
 * Concatenate all non-nullptr input operator_nodes.
 */
constexpr framework::operator_node_pointers_t concat_operator_nodes(const auto&... operator_nodes) {
  static_assert(sizeof...(operator_nodes) != 0);
  framework::operator_node_pointers_t concat_operator_nodes{};
  concat_operator_nodes.reserve(sizeof...(operator_nodes));

  const auto concat{[&concat_operator_nodes]<typename T>(const T& arg) {
    if constexpr (std::is_same_v<T, framework::operator_node*>) {
      if (arg != nullptr) {  // arg is operator_node*
        concat_operator_nodes.push_back(arg);
      }
    } else if constexpr (std::is_same_v<T, framework::operator_node_pointers_t>) {
      for (auto* const operator_node : arg) {  // arg is std::vector<operator_node*>
        if (operator_node != nullptr) {
          concat_operator_nodes.push_back(operator_node);
        }
      }
    } else {
      static_assert(legacy_embedded_ctl::always_false_v<decltype(arg)>, "Invalid operator nodes argument!");
    }
  }};

  (concat(operator_nodes), ...);

  legacy_embedded_debug_assert(!concat_operator_nodes.empty());
  return concat_operator_nodes;
}

}  // namespace details

/**
 * Types to indicate that no, all or some specific inputs of an operator_node have to be dictified.
 */
namespace dictify {

/*
 * Type, only used be the default implementation of operator_node, to indicate that it is unknown which inputs have to
 * be dictified. Should be removed once all operator_nodes expose their dictification requirements explicitly.
 */
struct unknown_inputs {
  constexpr explicit unknown_inputs() = default;
};

struct no_inputs {
  constexpr explicit no_inputs() = default;
};

struct all_inputs {
  constexpr explicit all_inputs() = default;
};

struct specific_inputs {
  template <typename... ARGS>
  requires(sizeof...(ARGS) > 0 &&
           (legacy_embedded_ctl::is_one_of_v<ARGS, framework::operator_node*, framework::operator_node_pointers_t> &&
            ...)) explicit specific_inputs(const ARGS&... operator_nodes)
      : operator_nodes_{details::concat_operator_nodes(operator_nodes...)} {}

  framework::operator_node_pointers_t operator_nodes_;
};

[[nodiscard]] bool shall_dictify_pull_up_column(const memory::column_t& column, const memory::table* target);

}  // namespace dictify

using dictify_t = std::variant<dictify::unknown_inputs,    // inputs that have to be dictified is unknown
                               dictify::no_inputs,         // no inputs have to be dictified
                               dictify::all_inputs,        // all inputs have to be dictified
                               dictify::specific_inputs>;  // only specified inputs have to be dictified

/** Type to indicate whether implicit dictification shall occur and to provide details for that dictification. */
class no_dictify_request {
 public:
  constexpr explicit no_dictify_request(legacy_embedded_ctl::source_location source_location =
                                            legacy_embedded_ctl::source_location{
                                                std::experimental::source_location::current()})
      : source_location_{source_location} {}

  [[nodiscard]] constexpr const legacy_embedded_ctl::source_location& get_source_location() const {
    return source_location_;
  }

 private:
  legacy_embedded_ctl::source_location source_location_;
};

}  // namespace celonis::accelerator::operators
