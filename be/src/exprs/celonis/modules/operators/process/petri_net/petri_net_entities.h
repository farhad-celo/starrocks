#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <boost/dynamic_bitset.hpp>

#include "legacy_embedded_ctl/hash.h"
#include "legacy_embedded_ctl/utility.h"
#include "modules/common/int_types.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

/// Strong type for place id
struct petri_net_place_id {
  uint16_t id{0};

  constexpr petri_net_place_id() noexcept = default;
  explicit constexpr petri_net_place_id(uint16_t id) noexcept : id{id} {}
  [[nodiscard]] constexpr bool operator==(const petri_net_place_id& other) const noexcept { return id == other.id; }
};

/// Strong type for transition id
struct petri_net_transition_id {
  uint16_t id{0};

  static constexpr petri_net_transition_id create_null_transition() noexcept { return petri_net_transition_id(0); }

  /// Default constructor (NULL transition)
  constexpr petri_net_transition_id() noexcept = default;
  explicit constexpr petri_net_transition_id(uint16_t id) noexcept : id{id} {}

  [[nodiscard]] constexpr bool is_null() const noexcept { return id == 0; }

  // NOLINTNEXTLINE(modernize-use-nullptr,readability-implicit-bool-conversion)
  auto operator<=>(const petri_net_transition_id& rhs) const = default;
};

struct petri_net_place {
  petri_net_place_id place{};
  std::string place_str_id{};
  std::vector<petri_net_transition_id> out_transitions{};

  petri_net_place() = default;
};

struct petri_net_transition {
  row_id label{};
  petri_net_transition_id transition{};
  std::string transition_str_id{};
  std::vector<petri_net_place_id> in_places{};
  std::vector<petri_net_place_id> out_places{};

  petri_net_transition() = default;
};

template <size_t SIZE_IN_BYTES>
class static_bitset {
 public:
  using size_type = size_t;
  static_assert(SIZE_IN_BYTES > sizeof(size_type));
  static constexpr size_t NUM_BITS{(SIZE_IN_BYTES - sizeof(size_type)) * 8};
  using bitset_type = std::bitset<NUM_BITS>;
  static_assert(sizeof(bitset_type) + sizeof(size_type) == SIZE_IN_BYTES);

  static_bitset() = default;
  explicit static_bitset(std::size_t s) : bitset_{}, size_{s} { legacy_embedded_ctl::abort_assert(s <= NUM_BITS * 8); }
  [[nodiscard]] constexpr std::size_t hash_value() const {
    size_t hash_value{0};
    legacy_embedded_ctl::hash_combine(hash_value, size_);
    legacy_embedded_ctl::hash_combine(hash_value, bitset_);
    return hash_value;
  }
  [[nodiscard]] constexpr std::size_t size() const { return size_; }
  [[nodiscard]] bool test(std::size_t pos) const {
    legacy_embedded_ctl::abort_assert(pos < size_);
    return bitset_.test(pos);
  }
  void set(std::size_t pos) {
    legacy_embedded_ctl::abort_assert(pos < size_);
    bitset_[pos] = true;
  }
  void reset(std::size_t pos) {
    legacy_embedded_ctl::abort_assert(pos < size_);
    bitset_[pos] = false;
  }
  [[nodiscard]] bool operator==(const static_bitset& other) const { return bitset_ == other.bitset_; }
  [[nodiscard]] constexpr bool operator<(const static_bitset& other) const {
    for (size_t i = 0; i < std::min(size(), other.size()); i++) {
      auto this_i{size() - i - 1};
      auto other_i{other.size() - i - 1};

      if (bitset_[this_i] ^ other.bitset_[other_i]) {
        return other.bitset_[other_i];
      }
    }
    return size() < other.size();
  }
  [[nodiscard]] bool none() const { return bitset_.none(); }

 private:
  bitset_type bitset_;
  std::size_t size_{};
};

/**
 * clang-tidy incorrectly deduces the implicitly defaulted move assignment operator to be noexcept and throws a false
 * positive error: an exception may be thrown in function 'operator=' which should not throw exceptions
 * [bugprone-exception-escape,-warnings-as-errors]
 * TODO (i.angelucci) check whether this is still needed with next clang-tidy update
 *  (bugprone-exception-escape was also added to other parts of the code that use marking_type, so check there too)
 */
// NOLINTNEXTLINE(bugprone-exception-escape)
class marking_type {
 public:
  using dynamic_bitset_type = boost::dynamic_bitset<>;
  using static_bitset_type = static_bitset<sizeof(dynamic_bitset_type)>;
  using small_bit_set = std::variant<static_bitset_type, dynamic_bitset_type>;

  marking_type() = default;
  explicit marking_type(small_bit_set marking) : bitset_{std::move(marking)} {}
  explicit marking_type(std::size_t size) {
    if (size <= static_bitset_type::NUM_BITS) {
      bitset_ = static_bitset_type(size);
    } else {
      bitset_ = dynamic_bitset_type(size);
    }
  }

  [[nodiscard]] constexpr std::size_t hash_value() const {
    return std::visit(legacy_embedded_ctl::overloaded{[](const static_bitset_type& arg) { return arg.hash_value(); },
                                      [](const dynamic_bitset_type& arg) {
                                        std::hash<dynamic_bitset_type> hash;
                                        return hash(arg);
                                      }},
                      bitset_);
  }

  [[nodiscard]] constexpr std::size_t size() const {
    return std::visit([](auto& arg) { return arg.size(); }, bitset_);
  }
  [[nodiscard]] bool test(std::size_t pos) const {
    return std::visit([pos](auto& arg) { return arg.test(pos); }, bitset_);
  }
  void set(std::size_t pos) {
    return std::visit([pos](auto& arg) { arg.set(pos); }, bitset_);
  }
  void reset(std::size_t pos) {
    return std::visit([pos](auto& arg) { arg.reset(pos); }, bitset_);
  }

  [[nodiscard]] bool operator==(const marking_type& other) const {
    legacy_embedded_ctl::abort_assert(size() == other.size());
    return bitset_ == other.bitset_;
  }
  [[nodiscard]] bool operator<(const marking_type& other) const {
    legacy_embedded_ctl::abort_assert(size() == other.size());
    return bitset_ < other.bitset_;
  }

  [[nodiscard]] bool empty() const {
    return std::visit([](auto& arg) { return arg.none(); }, bitset_);
  };

 private:
  small_bit_set bitset_;
};

struct trace_variable {
  uint16_t id{0};

  constexpr trace_variable() noexcept = default;
  explicit constexpr trace_variable(uint16_t variable_id) noexcept : id{variable_id} {}

  [[nodiscard]] constexpr bool operator==(const trace_variable& other) const noexcept { return id == other.id; }
  [[nodiscard]] constexpr bool operator!=(const trace_variable& other) const noexcept { return id != other.id; }
};

struct variable_to_transition_assignment {
  trace_variable variable{};
  petri_net_transition_id tgt_transition{};

  [[nodiscard]] constexpr bool operator==(const variable_to_transition_assignment& other) const noexcept {
    return variable == other.variable && tgt_transition == other.tgt_transition;
  }

  /// Default constructor (empty variable assigned to NULL transition)
  constexpr variable_to_transition_assignment() noexcept = default;
  constexpr variable_to_transition_assignment(trace_variable variable, petri_net_transition_id label) noexcept
      : variable{variable}, tgt_transition{label} {}
  [[nodiscard]] constexpr bool is_null_assignment() const noexcept { return tgt_transition.is_null(); }
};

using rl_problem_solution_t = std::vector<variable_to_transition_assignment>;

inline uint64_t get_cost(const rl_problem_solution_t& solution) noexcept {
  return std::count_if(std::cbegin(solution), std::cend(solution),
                       [](const auto& assignment) { return assignment.is_null_assignment(); });
}

struct hash_place {
  [[nodiscard]] constexpr std::size_t operator()(const petri_net_place_id& place) const noexcept { return place.id; }
};

struct hash_transition {
  [[nodiscard]] constexpr std::size_t operator()(const petri_net_transition_id& transition) const noexcept {
    return transition.id;
  }
};

/// Needed for boost::hash
constexpr std::size_t hash_value(const petri_net_place_id& place) noexcept { return hash_place{}(place); }

constexpr std::size_t hash_value(const petri_net_transition_id& transition) noexcept {
  return hash_transition{}(transition);
}

inline std::size_t hash_value(const marking_type& marking) { return marking.hash_value(); }

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
