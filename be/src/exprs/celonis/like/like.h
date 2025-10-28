#pragma once

#include <ctl/named_type.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "exprs/celonis/match_pattern_util.h"
#include "util/slice.h"

// Inspired by https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/native/cpm-accelerator/modules/cube/operator_compare/operator_string_compare_functions.h

namespace starrocks::celonis::like {

/* After pre-processing a pattern, a pattern can be classified as one of the following types. */
enum class pattern_type {
    // Pattern contains no wildcards:
    NO_WILDCARD_AND_NO_ESCAPED_WILDCARDS,   // No wildcards and not even escaped wildcards (e.g. 'pattern')
    NO_WILDCARD_BUT_WITH_ESCAPED_WILDCARDS, // No wildcards but contains at least one escaped wildcard (e.g. 'pat\%tern')

    // Pattern: '%' or '' -> matches everything
    MATCH_EVERYTHING,

    // Special optimized cases: Pattern contains at least one wildcard and *no escaped wildcards*:
    WILDCARD_STARTS_WITH, // Exactly one % at the end e.g. 'pattern%'
    WILDCARD_ENDS_WITH,   // Exactly one % at the beginning e.g. '%pattern'
    WILDCARD_CONTAINS,    // Exactly one % at the beginning and end e.g. '%pattern%'

    // Every other pattern (e.g. '%pat_tern')
    WILDCARD
};

/* The result of pre-processing a pattern. */
struct preprocessed_pattern {
    /**
   * If the pattern contained more than one leading or trailing percent wildcard ('%'), this value represents the
   * smaller - and equivalent - pattern with the superfluous wildcards removed.
   *
   * Example:
   * Input pattern: '%%%foo%%'
   * Trimmed pattern: '%foo%'
   */
    std::string_view trimmed_pattern_;

    /** The pattern type (see above). */
    pattern_type type_;

    /** Returns true if the pattern contains no wildcards (but potentially escaped wildcards) */
    [[nodiscard]] bool has_no_wildcards() const noexcept {
        return type_ == pattern_type::NO_WILDCARD_AND_NO_ESCAPED_WILDCARDS ||
               type_ == pattern_type::NO_WILDCARD_BUT_WITH_ESCAPED_WILDCARDS;
    }

    /** Returns true if the pattern has at least one non-escaped wildcard. */
    [[nodiscard]] bool has_wildcards() const noexcept { return !has_no_wildcards(); }
};

/**
 * Pre-processes a pattern.
 * @param original_pattern null-terminated pattern string
 * @return See above for more information on the result type.
 */
[[nodiscard]] preprocessed_pattern preprocess_pattern(pattern_t original_pattern);

/**
 * Returns a newly allocated string, in which all escaped wildcards are replaced their representing character.
 * Examples:
 * 'foo' -> 'foo'
 * 'foo\%foo' -> 'foo%foo'
 * '%foo\%foo' -> '%foo%foo'
 */
[[nodiscard]] std::string sanitize_pattern(std::string_view pattern);

/**
 * Implementation for LIKE which is intended to be used multiple times, i.e. multiple input strings are matched against
 * a single constant pattern. Use this implementation when a pattern is supposed to be matched more than once.
 *
 * Since the pattern is used multiple times, the pattern is copied into a newly allocated string and potentially
 * sanitized, which allows faster pattern matching.
 */
class like_reusable final {
public:
    /**
   * Creates a reusable like implementation given the pattern string and an execution_context, in which potential
   * warnings are inserted.
   */
    like_reusable(pattern_t pattern);

    /* Matches the given input with the pattern according to the LIKE specification. */
    [[nodiscard]] bool operator()(std::string_view input) const;

    [[nodiscard]] bool is_case_sensitive() const;

private:
    std::string pattern_;
    pattern_type pattern_type_{};
    bool is_case_sensitive_ = true;
};

std::string lower_string_utf8(std::string_view input);

void lower_string_utf8_inplace(std::string& pattern);

/**
 * To be constructed for every input row. Caches modifications to the input so it is re-used for multiple patterns if possible.
 */
class input_row_pattern_matcher {
public:
    input_row_pattern_matcher(std::string_view input) : input_{input} {};

    bool match(const like_reusable& pattern);

private:
    std::string_view input_;
    std::optional<std::string> lower_cased_input_;
};

} // namespace starrocks::celonis::like
