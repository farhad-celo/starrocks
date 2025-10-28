#pragma once
#include <ctl/named_type.h>

#include <optional>
#include <string_view>

namespace starrocks::celonis {

constexpr char ESCAPE_CHAR{'\\'};
constexpr char MATCH_ONE{'_'};
constexpr char MATCH_ANY{'%'};

using pattern_t = ctl::named_type<std::string_view, struct pattern_sv_tag>;

int64_t pattern_index(const char* const text, const char* const pattern, const int64_t occurrence);

/**
 * Matches the given pattern against the given input. Both inputs must be null-terminated strings.
 * The pattern may contain wildcard characters:
 * - The underscore ('_') matches exactly one character.
 * - The percent sign ('%') matches any string with zero or more characters.
 * The wildcards can be escaped using the backslash ('\') for matching the literal wildcard character.
 *
 * This function will *always* perform pattern matching, no matter how the pattern looks like. If you wish to implement
 * behavior similar to LIKE, this function should NOT be called directly. LIKE behaves differently depending on the
 * amount of wildcards being used, which is not the case for this function.
 *
 * @return true if the pattern matches
 */
[[nodiscard]] bool match_pattern(std::string_view input, pattern_t pattern_sv);

namespace details {
struct matching_position {
    const char* input_itr;
    const char* pattern_itr;
};

/**
 * This implements the ends_with step of the match_pattern algorithm.
 * The behavior and input/output is analogous to match_pattern_starts_with.
 */
[[nodiscard]] std::optional<matching_position> match_pattern_ends_with(std::string_view input,
                                                                       std::string_view pattern);

/**
 * This function implements the starts_with step of the pattern matching algorithm. It checks if the first characters
 * of the pattern (until a % is found or the pattern is depleted) appear in that order in the input. Escaped
 * characters in the pattern are taken into account.
 *
 * If the input doesn't start with the characters from the pattern, std::nullopt is returned.
 *
 * If the input starts with the characters from the pattern, an optional of a matching position is returned. This
 * contains iterators to the input and pattern which have been consumed.
 *
 * Example: Input "abcXXXXdef" and pattern "abc%def"
 * Output: matching position with an iterator to the first X of the input and an iterator to the % of the pattern
 *
 * Example: Input "XXXXdef" and pattern "abc%def"
 * Output: empty optional
 */
[[nodiscard]] std::optional<matching_position> match_pattern_starts_with(std::string_view input,
                                                                         std::string_view pattern);

/**
 * This function implements the "contains" step of the match_pattern algorithm. It checks if all character sequences
 * between 2 surrounding % signs are contained in the input in that order. The input must be trimmed to the sequences
 * which should be matched.
 * Example:
 * Input "abcXXXXqrs" and pattern "abc%def%ghijkl%%mnop%qrs"
 *           |   |                     |                |
 *   input begin end           pattern begin            end
 *
 * Input should be "XXXX" and pattern should be "def%ghijkl%%mnop%".
 *
 * This function returns a starting position of the match if all character sequences appear in the input,
 * otherwise std::nullopt.
 */
[[nodiscard]] std::optional<matching_position> match_pattern_contains(std::string_view input, std::string_view pattern);
} // namespace details

} // namespace starrocks::celonis
